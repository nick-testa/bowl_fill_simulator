#include "core/measurements.hh"
#include "core/feasibility.hh"
#include "core/fitted_curves.hh"
#include "core/step_table.hh"
#include "core/sweep.hh"
#include "core/volume_fill_export.hh"
#include "ui/main_window.hh"
#include "ui/manual_entry_settings.hh"
#include "ui/qt_text.hh"
#include "ui/theme.hh"
#include "ui/units.hh"

#include <QApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QSettings>
#include <QTimer>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

/// The menus as the window would load them: with the saved name mappings and piece
/// heights applied, so scripted reports agree with what the app shows.
bowlfill::LoadResult load_configured_menus(const QString &asset_dir)
{
    using namespace bowlfill;
    NameMappings maps;
    PieceHeights pieces;
    std::string err;
    if (!load_name_mappings(ss(asset_dir + "/data/ingredient_aliases.csv"), maps, &err))
        QTextStream(stderr) << qs(err) << "\n";
    if (!load_piece_heights(ss(asset_dir + "/data/ingredient_pieces.csv"), pieces, &err))
        QTextStream(stderr) << qs(err) << "\n";
    LoadResult loaded = load_menus(ss(asset_dir + "/menus"), &maps, &pieces);
    // The stations set in the window, when there are any.
    if (const ManualEntry manual = load_manual_entry(); !manual.empty())
        loaded.menus.push_back(manual_entry_menu(loaded.menus, manual, &pieces));
    return loaded;
}

/// The bowl, lid and cup dimensions last set in the window. The height model is on
/// unless the window turned it off or --volume-only asks for the old model.
bowlfill::BowlGeometry configured_geometry(bool volume_only)
{
    using namespace bowlfill;
    const QSettings prefs;
    BowlGeometry g;
    g.enabled = !volume_only && prefs.value("geometry/enabled", true).toBool();
    g.depth_mm = prefs.value("bowl/depth_mm", g.depth_mm).toDouble();
    g.top_diameter_mm = prefs.value("bowl/top_diameter_mm", g.top_diameter_mm).toDouble();
    g.bottom_diameter_mm =
        prefs.value("bowl/bottom_diameter_mm", g.bottom_diameter_mm).toDouble();
    g.lid_headroom_mm = prefs.value("lid/headroom_mm", g.lid_headroom_mm).toDouble();
    g.lid_plateau_diameter_mm =
        prefs.value("lid/plateau_diameter_mm", g.lid_plateau_diameter_mm).toDouble();
    g.cups_pressed = prefs.value("geometry/cups_pressed", g.cups_pressed).toBool();
    g.cup_height_mm = prefs.value("cups/height_mm", g.cup_height_mm).toDouble();
    g.cup_diameter_mm = prefs.value("cups/top_diameter_mm", g.cup_diameter_mm).toDouble();
    g.cup_base_diameter_mm =
        prefs.value("cups/base_diameter_mm", g.cup_base_diameter_mm).toDouble();
    g.chunk_proud = prefs.value("geometry/chunk_proud_pct", g.chunk_proud * 100).toDouble() / 100;
    return g;
}

/// Volume Fill's fit target and step-down limit, as last set in the window.
bowlfill::VolumeFillSettings configured_volume_fill()
{
    const QSettings prefs;
    bowlfill::VolumeFillSettings vf;
    vf.fit_share = prefs.value("volume_fill/fit_pct", vf.fit_share * 100).toDouble() / 100;
    vf.min_share = prefs.value("volume_fill/min_pct", vf.min_share * 100).toDouble() / 100;
    return vf;
}

/// The dispense variation last set in the window, for the reports' overfill odds.
bowlfill::DispenseVariation configured_dispense()
{
    const QSettings prefs;
    bowlfill::DispenseVariation d;
    d.base = prefs.value("dispense/base_pct", d.base * 100).toDouble() / 100;
    d.protein = prefs.value("dispense/protein_pct", d.protein * 100).toDouble() / 100;
    d.topping = prefs.value("dispense/topping_pct", d.topping * 100).toDouble() / 100;
    d.base_bias = prefs.value("dispense/base_bias_pct", d.base_bias * 100).toDouble() / 100;
    d.protein_bias =
        prefs.value("dispense/protein_bias_pct", d.protein_bias * 100).toDouble() / 100;
    d.topping_bias =
        prefs.value("dispense/topping_bias_pct", d.topping_bias * 100).toDouble() / 100;
    return d;
}

/// The measurement store the window loads, data/measurements, with the pool groups and
/// fitted curves the window uses. Reports files that could not be read, the state of the
/// fitted curves (unless `quiet_fit`), and the squash fitted from the mixed bowls on
/// file. Like the window, the reports never apply that fit; they use the default
/// settings. False when there is nothing to fit curves from.
bool load_cli_curves(const QString &asset_dir, bowlfill::CurveSet &curves,
                     bowlfill::SquashFit *squash, bool quiet_fit = false)
{
    using namespace bowlfill;
    QTextStream err(stderr);
    IngredientGroups groups;
    std::string group_err;
    if (!load_ingredient_groups(ss(asset_dir + "/data/ingredient_groups.csv"), groups, &group_err))
        err << qs(group_err) << "\n";
    curves.set_groups(groups);
    const QSettings prefs;
    MeasurementOptions options;
    options.empty_bowl_ml =
        prefs.value("measurements/empty_bowl_ml", kDefaultEmptyBowlMl).toDouble();
    options.backfill = prefs.value("measurements/backfill", false).toBool();
    options.drop_impossible = prefs.value("measurements/drop_impossible", false).toBool();
    const MeasurementSet ms = load_measurements(ss(asset_dir + kMeasurementsDir), curves, options);
    for (const MeasurementFile &f : ms.files)
        if (f.failed) err << "data/measurements/" << qs(f.name) << ": " << qs(f.problem) << "\n";
    if (curves.observations().empty()) {
        err << "no usable measurements in " << asset_dir << kMeasurementsDir << "\n";
        return false;
    }
    FittedCurves fitted;
    std::string fit_err;
    if (!load_fitted_curves(ss(asset_dir + "/data/fitted_curves.json"), fitted, &fit_err))
        err << qs(fit_err) << "\n";
    curves.set_fitted(fitted);
    if (!quiet_fit)
        if (const std::string note = fit_status_message(fit_status(curves), curves.fitted()); !note.empty())
            err << qs(note) << "\n";
    *squash = fit_squash(ms.bowls, curves);
    return true;
}

/// Writes the points the curves rest on, for tools/fit_curves.py.
int run_export_fit_input(const QString &asset_dir, const QString &path)
{
    using namespace bowlfill;
    CurveSet curves;
    SquashFit squash;
    if (!load_cli_curves(asset_dir, curves, &squash, true)) return 2;
    std::string err;
    if (!write_fit_input(ss(path), curves, &err)) {
        QTextStream(stderr) << qs(err) << "\n";
        return 2;
    }
    return 0;
}

/// Headless feasibility audit, so the culinary team's report can be regenerated in a
/// script without opening the window.
int run_audit(const QString &asset_dir, const QString &want, bool volume_only)
{
    using namespace bowlfill;
    QTextStream out(stdout), err(stderr);

    CurveSet curves;
    SquashFit squash;
    if (!load_cli_curves(asset_dir, curves, &squash)) return 2;
    CostTable costs;
    costs.load_csv(ss(asset_dir + "/data/ingredient_costs.csv"));

    const LoadResult loaded = load_configured_menus(asset_dir);
    if (loaded.menus.empty()) {
        err << "no menus under " << asset_dir << "/menus\n";
        return 2;
    }

    bool header_written = false, matched = false;
    for (const Menu &m : loaded.menus) {
        if (want.compare("all", Qt::CaseInsensitive) != 0
            && qs(m.brand).compare(want, Qt::CaseInsensitive) != 0)
            continue;
        matched = true;
        SimSettings legacy;
        AdaptiveSettings adaptive;
        legacy.geometry = adaptive.geometry = configured_geometry(volume_only);
        legacy.dispense = adaptive.dispense = configured_dispense();
        const BrandAudit audit = audit_brand(m, curves, legacy, adaptive, &costs,
                                             configured_volume_fill());
        const std::string csv = audit.to_csv();
        out << qs(header_written ? csv.substr(csv.find('\n') + 1) : csv);
        header_written = true;
    }
    if (!matched) {
        err << "no brand matching \"" << want << "\". Known brands:\n";
        for (const Menu &m : loaded.menus) err << "  " << qs(m.brand) << "\n";
        return 2;
    }
    return 0;
}

/// Writes lab37's volume_fill_curves.cc (the default curve of every ingredient, keyed by
/// production name, and Volume Fill's settings as last set in the window) and, given a
/// manifest path, the JSON of values for the Catalog's volume_curve fields. Compression and
/// the bowl's capacity are not remembered by the window, so they take its defaults.
int run_export_volume_fill(const QString &asset_dir, const QString &path, const QString &manifest_path)
{
    using namespace bowlfill;
    QTextStream err(stderr);

    CurveSet curves;
    SquashFit squash;
    if (!load_cli_curves(asset_dir, curves, &squash)) return 2;
    const LoadResult loaded = load_configured_menus(asset_dir);

    SimSettings settings;
    settings.geometry = configured_geometry(false);
    settings.compress = true;
    settings.load_transfer = 1.0;
    std::vector<std::string> active;
    std::string active_err;
    if (!load_active_brands(ss(asset_dir + "/data/active_brands.csv"), active, &active_err))
        err << qs(active_err) << "\n";
    const VolumeFillExport ex =
        export_volume_fill(loaded.menus, curves, settings, configured_volume_fill(), active);
    err << "brands: " << qs(join(ex.brands, ", ")) << "\n";
    if (!ex.skipped.empty())
        err << "left out (not in data/active_brands.csv): " << qs(join(ex.skipped, ", ")) << "\n";
    for (const std::string &b : ex.missing)
        err << "data/active_brands.csv lists \"" << qs(b) << "\", but no menu has that brand\n";
    for (const std::string &c : ex.conflicts) err << "brands disagree: " << qs(c) << "\n";
    if (!write_text(ss(path), ex.source)) {
        err << "could not write " << path << "\n";
        return 2;
    }
    err << ex.rows.size() << " ingredients written to " << path << "\n";
    if (!manifest_path.isEmpty()) {
        if (!write_text(ss(manifest_path), ex.manifest)) {
            err << "could not write " << manifest_path << "\n";
            return 2;
        }
        err << "Catalog values written to " << manifest_path << "\n";
    }
    return 0;
}

/// Headless volume-per-gram table: what each ingredient's step adds, and the step that
/// would add `target_ml`, for choosing volume-proportional steps.
int run_steps(const QString &asset_dir, const QString &want, double target_ml)
{
    using namespace bowlfill;
    QTextStream out(stdout), err(stderr);

    CurveSet curves;
    SquashFit squash;
    if (!load_cli_curves(asset_dir, curves, &squash)) return 2;
    const LoadResult loaded = load_configured_menus(asset_dir);

    bool header_written = false, matched = false;
    for (const Menu &m : loaded.menus) {
        if (want.compare("all", Qt::CaseInsensitive) != 0
            && qs(m.brand).compare(want, Qt::CaseInsensitive) != 0)
            continue;
        matched = true;
        const std::string csv =
            step_table_csv(m.brand, step_table(m, curves, target_ml), target_ml);
        out << qs(header_written ? csv.substr(csv.find('\n') + 1) : csv);
        header_written = true;
    }
    if (!matched) {
        err << "no brand matching \"" << want << "\". Known brands:\n";
        for (const Menu &m : loaded.menus) err << "  " << qs(m.brand) << "\n";
        return 2;
    }
    return 0;
}

/// Headless combinatorial workup, for regenerating the full report in a script.
int run_sweep_cli(const QString &asset_dir, const QString &want, const QString &out_path,
                  int max_toppings, int max_proteins, int sauces, bool volume_only)
{
    using namespace bowlfill;
    QTextStream err(stderr);

    CurveSet curves;
    SquashFit squash;
    if (!load_cli_curves(asset_dir, curves, &squash)) return 2;
    CostTable costs;
    costs.load_csv(ss(asset_dir + "/data/ingredient_costs.csv"));

    const LoadResult loaded = load_configured_menus(asset_dir);
    std::vector<const Menu *> menus;
    for (const Menu &m : loaded.menus)
        if (want.compare("all", Qt::CaseInsensitive) == 0
            || qs(m.brand).compare(want, Qt::CaseInsensitive) == 0)
            menus.push_back(&m);
    if (menus.empty()) {
        err << "no brand matching \"" << want << "\". Known brands:\n";
        for (const Menu &m : loaded.menus) err << "  " << qs(m.brand) << "\n";
        return 2;
    }

    SweepLimits limits;
    if (max_toppings >= 0) limits.max_toppings = max_toppings;
    if (max_proteins >= 0) limits.max_proteins = max_proteins;
    if (sauces > 0) limits.min_sauces = limits.max_sauces = sauces;

    const bool to_stdout = out_path.isEmpty() || out_path == "-";
    std::ofstream file;
    if (!to_stdout) {
        file.open(ss(out_path), std::ios::binary | std::ios::trunc);
        if (!file) {
            err << "cannot write " << out_path << "\n";
            return 2;
        }
    }
    std::ostream &out = to_stdout ? std::cout : file;

    std::int64_t last_pct = -1;
    std::vector<SweepTotals> totals;
    SimSettings legacy;
    AdaptiveSettings adaptive;
    legacy.geometry = adaptive.geometry = configured_geometry(volume_only);
    legacy.dispense = adaptive.dispense = configured_dispense();
    run_sweep(menus, curves, legacy, adaptive, &costs,
              limits, out, [&](std::int64_t done, std::int64_t total) {
                  const std::int64_t pct = total > 0 ? done * 100 / total : 100;
                  if (pct != last_pct) {
                      last_pct = pct;
                      err << "\r" << static_cast<qint64>(pct) << "%" << Qt::flush;
                  }
                  return true;
              },
              &totals, configured_volume_fill());
    out.flush();

    SweepTotals all;
    for (const SweepTotals &t : totals) {
        all.rows += t.rows;
        all.legacy_fits += t.legacy_fits;
        all.legacy_over += t.legacy_over;
        all.legacy_short += t.legacy_short;
        all.adaptive_over += t.adaptive_over;
        all.adaptive_over_bowl += t.adaptive_over_bowl;
        all.adaptive_short += t.adaptive_short;
        all.volume_fits += t.volume_fits;
        all.volume_short += t.volume_short;
        all.volume_over += t.volume_over;
    }
    err << "\r" << all.rows << " bowls; legacy: " << all.legacy_fits << " right, "
        << all.legacy_over << " over the bowl, " << all.legacy_short
        << " short of the floor. Adaptive: " << all.adaptive_over_bowl
        << " still do not fit, "
        << all.adaptive_short << " cannot reach the fill target. Volume fill: "
        << all.volume_fits << " fit, " << all.volume_short << " at max under the limit, "
        << all.volume_over << " still over.\n";
    return 0;
}


/// Where menus/, data/ and photos/ are read from when --assets is not given.
///
/// Run from a source build, that is the source tree. Run as the macOS app, the bundle
/// carries a copy in Resources/assets, but the bundle is read-only and replaced on every
/// update, so the copy is laid out in Application Support and used from there. A build
/// with a different stamp replaces the menus, photos and reference CSVs, keeping the
/// old CSVs under backups/ since the Ingredients dialog edits them; measurements and
/// scenarios the user added are never overwritten.
QString default_asset_dir()
{
    const QString bundled = QCoreApplication::applicationDirPath() + "/../Resources/assets";
    QFile bundle_stamp(bundled + "/.stamp");
    if (!bundle_stamp.open(QIODevice::ReadOnly)) return BOWLFILL_ASSET_DIR;
    const QByteArray stamp = bundle_stamp.readAll();

    const QString user = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile user_stamp(user + "/.stamp");
    QByteArray previous;
    if (user_stamp.open(QIODevice::ReadOnly)) previous = user_stamp.readAll();
    user_stamp.close();
    if (previous == stamp) return user;

    if (!previous.isEmpty()) {
        const QString backup =
            user + "/backups/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QDir().mkpath(backup);
        for (const QFileInfo &f : QDir(user + "/data").entryInfoList({"*.csv"}, QDir::Files))
            QFile::copy(f.filePath(), backup + "/" + f.fileName());
    }
    // Menus deleted from the repo should disappear too, so start that folder afresh.
    QDir(user + "/menus").removeRecursively();

    QDirIterator it(bundled, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    const QDir from(bundled);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = from.relativeFilePath(src);
        if (rel == ".stamp") continue;
        const QString dst = user + "/" + rel;
        const bool users_own =
            rel.startsWith("data/measurements/") || rel.startsWith("data/scenarios/");
        if (QFileInfo::exists(dst)) {
            if (users_own) continue;
            QFile::remove(dst);
        }
        QDir().mkpath(QFileInfo(dst).path());
        QFile::copy(src, dst);
        QFile::setPermissions(dst, QFile::permissions(dst) | QFile::WriteUser);
    }

    if (user_stamp.open(QIODevice::WriteOnly | QIODevice::Truncate)) user_stamp.write(stamp);
    return user;
}

}  // namespace

int main(int argc, char **argv)
{
    // A 130 DPI display wants about 1.35x. Rounding that to 1x (the pre-6.x default on
    // some platforms) renders small text into too few pixels; PassThrough keeps the
    // fractional factor so glyphs are rasterised at the size they are shown.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // --scale has to be applied before the QApplication reads the environment.
    for (int i = 1; i + 1 < argc; ++i)
        if (QString(argv[i]) == "--scale") qputenv("QT_SCALE_FACTOR", argv[i + 1]);

    // Qt's Wayland plugin warns each time a tooltip closes ("Trying to disable ... but
    // 0x0 is focused"): tooltips never take keyboard focus, so there is nothing to
    // disable. Harmless and only noise. QT_LOGGING_RULES still overrides this.
    QLoggingCategory::setFilterRules("qt.qpa.wayland.textinput.warning=false");

    QApplication app(argc, argv);
    app.setApplicationName("Bowl Fill Simulator");
    app.setOrganizationName("Lab37");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Simulates the dynamic portion algorithm against a bowl's volume limit.");
    parser.addHelpOption();
    QCommandLineOption assets(
        {"a", "assets"},
        "Directory holding menus/, data/ and photos/ (default: the build's source dir, "
        "or Application Support when run as the macOS app).",
        "dir");
    parser.addOption(assets);
    QCommandLineOption shot("screenshot",
                            "Render the window to a PNG and exit. Useful for docs and "
                            "for checking layout without a display server.",
                            "file");
    parser.addOption(shot);
    QCommandLineOption dark("dark", "Force the dark palette.");
    parser.addOption(dark);
    QCommandLineOption unit("units", "Volume units: oz or ml.", "u");
    parser.addOption(unit);
    QCommandLineOption mode("mode", "Dispense model: legacy, adaptive or volume.", "m");
    parser.addOption(mode);
    QCommandLineOption scale("scale",
                             "Render at this device pixel ratio, e.g. 2 for a sharp "
                             "screenshot. Applied before the window is built.", "n");
    parser.addOption(scale);
    QCommandLineOption size("size", "Window size as WxH, e.g. 1400x1600.", "WxH");
    parser.addOption(size);
    QCommandLineOption pick("select",
                            "Preselect \"Brand|Recipe\" before rendering.", "spec");
    parser.addOption(pick);
    QCommandLineOption audit("audit",
                             "Print the feasibility report for a brand (or \"all\") as "
                             "CSV and exit, without opening a window.", "brand");
    parser.addOption(audit);
    QCommandLineOption steps("steps",
                             "Print each ingredient's volume per gram, what its step adds, "
                             "and the step that would add --step-ml, for a brand (or "
                             "\"all\") as CSV and exit.", "brand");
    parser.addOption(steps);
    QCommandLineOption step_ml("step-ml",
                               "Volume each step should add, for --steps (default 10 ml).",
                               "ml", "10");
    parser.addOption(step_ml);
    QCommandLineOption sweep("sweep",
                             "Run the full combinatorial workup for a brand (or "
                             "\"all\") and exit.", "brand");
    parser.addOption(sweep);
    QCommandLineOption sweep_out("out", "Where --sweep writes its CSV (default stdout).",
                                 "file");
    parser.addOption(sweep_out);
    QCommandLineOption max_top("max-toppings", "Topping cap for --sweep (default 4).", "n");
    parser.addOption(max_top);
    QCommandLineOption max_pro("max-proteins", "Protein cap for --sweep (default 2).", "n");
    parser.addOption(max_pro);
    QCommandLineOption sauces("sauces",
                              "Pin --sweep to this many sauce cups. Omit to sweep both "
                              "one and two.", "n");
    parser.addOption(sauces);
    QCommandLineOption volume_only("volume-only",
                                   "For --sweep and --audit: judge fit by volume alone, with "
                                   "cups at their contents, instead of by height under the lid.");
    parser.addOption(volume_only);
    QCommandLineOption export_vf("export-volume-fill",
                                 "Write lab37's volume_fill_curves.cc (default curves by "
                                 "production ingredient name, and Volume Fill's settings) and exit.",
                                 "file");
    parser.addOption(export_vf);
    QCommandLineOption catalog_manifest("catalog-manifest",
                                        "With --export-volume-fill, also write the values for the "
                                        "Catalog's volume_curve fields as JSON.",
                                        "file");
    parser.addOption(catalog_manifest);
    QCommandLineOption export_fit("export-fit-input",
                                  "Write the single-ingredient points the curves rest on (with their "
                                  "pool groups and batches) for tools/fit_curves.py, and exit. "
                                  "\"-\" writes to stdout.",
                                  "file");
    parser.addOption(export_fit);
    parser.process(app);
    const QString asset_dir = parser.isSet(assets) ? QDir(parser.value(assets)).absolutePath()
                                                   : default_asset_dir();

    if (parser.isSet(sweep))
        return run_sweep_cli(asset_dir, parser.value(sweep),
                             parser.value(sweep_out),
                             parser.isSet(max_top) ? parser.value(max_top).toInt() : -1,
                             parser.isSet(max_pro) ? parser.value(max_pro).toInt() : -1,
                             parser.isSet(sauces) ? parser.value(sauces).toInt() : -1,
                             parser.isSet(volume_only));

    if (parser.isSet(export_fit))
        return run_export_fit_input(asset_dir, parser.value(export_fit));

    if (parser.isSet(export_vf))
        return run_export_volume_fill(asset_dir, parser.value(export_vf),
                                      parser.value(catalog_manifest));

    if (parser.isSet(steps))
        return run_steps(asset_dir, parser.value(steps),
                         parser.value(step_ml).toDouble());

    if (parser.isSet(audit))
        return run_audit(asset_dir, parser.value(audit),
                         parser.isSet(volume_only));

    bowlfill::theme::follow_system();
    if (parser.isSet(dark)) bowlfill::theme::set_dark(true);
    if (parser.isSet(unit))
        bowlfill::units::set_from_cli(parser.value(unit).startsWith("m")
                                 ? bowlfill::units::Volume::Millilitres
                                 : bowlfill::units::Volume::FluidOunces);

    bowlfill::MainWindow window(asset_dir);
    if (parser.isSet(mode) && parser.value(mode).startsWith("a")) window.select_adaptive();
    if (parser.isSet(mode) && parser.value(mode).startsWith("v")) window.set_volume_fill();
    if (parser.isSet(pick)) {
        const QStringList parts = parser.value(pick).split('|');
        window.select(parts.value(0), parts.value(1));
    }
    if (parser.isSet(size)) {
        const QStringList wh = parser.value(size).split('x');
        if (wh.size() == 2) window.resize(wh[0].toInt(), wh[1].toInt());
    }
    window.show();

    if (parser.isSet(shot)) {
        QTimer::singleShot(400, &app, [&] {
            window.grab().save(parser.value(shot));
            app.quit();
        });
    }
    return app.exec();
}
