#include "core/feasibility.hh"
#include "core/sweep.hh"
#include "ui/main_window.hh"
#include "ui/theme.hh"
#include "ui/units.hh"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QTimer>

namespace {

/// The menus as the window would load them: with the saved name mappings and piece
/// heights applied, so scripted reports agree with what the app shows.
bowlfill::LoadResult load_configured_menus(const QString &asset_dir)
{
    using namespace bowlfill;
    NameMappings maps;
    PieceHeights pieces;
    QString err;
    if (!load_name_mappings(asset_dir + "/data/ingredient_aliases.csv", maps, &err))
        QTextStream(stderr) << err << "\n";
    if (!load_piece_heights(asset_dir + "/data/ingredient_pieces.csv", pieces, &err))
        QTextStream(stderr) << err << "\n";
    return load_menus(asset_dir + "/menus", &maps, &pieces);
}

/// The bowl, lid and cup dimensions last set in the window. The height model is on
/// unless the window turned it off or --volume-only asks for the old model.
bowlfill::BowlGeometry configured_geometry(bool volume_only)
{
    using namespace bowlfill;
    const QSettings prefs;
    BowlGeometry g;
    g.enabled = !volume_only && prefs.value("geometry/enabled", true).toBool();
    g.depth_mm = prefs.value("geometry/depth_mm", g.depth_mm).toDouble();
    g.lid_headroom_mm = prefs.value("geometry/lid_headroom_mm", g.lid_headroom_mm).toDouble();
    g.cups_pressed = prefs.value("geometry/cups_pressed", g.cups_pressed).toBool();
    g.cup_height_mm = prefs.value("geometry/cup_height_mm", g.cup_height_mm).toDouble();
    g.cup_diameter_mm = prefs.value("geometry/cup_diameter_mm", g.cup_diameter_mm).toDouble();
    g.chunk_proud = prefs.value("geometry/chunk_proud_pct", g.chunk_proud * 100).toDouble() / 100;
    return g;
}

/// Headless feasibility audit, so the culinary team's report can be regenerated in a
/// script without opening the window.
int run_audit(const QString &asset_dir, const QString &want, bool volume_only)
{
    using namespace bowlfill;
    QTextStream out(stdout), err(stderr);

    CurveSet curves;
    QString error;
    if (!curves.load_csv(asset_dir + "/data/mass_to_volume.csv", &error)) {
        err << "curves: " << error << "\n";
        return 2;
    }
    CostTable costs;
    costs.load_csv(asset_dir + "/data/ingredient_costs.csv");

    const LoadResult loaded = load_configured_menus(asset_dir);
    if (loaded.menus.empty()) {
        err << "no menus under " << asset_dir << "/menus\n";
        return 2;
    }

    bool header_written = false, matched = false;
    for (const Menu &m : loaded.menus) {
        if (want.compare("all", Qt::CaseInsensitive) != 0
            && m.brand.compare(want, Qt::CaseInsensitive) != 0)
            continue;
        matched = true;
        SimSettings legacy;
        AdaptiveSettings adaptive;
        legacy.geometry = adaptive.geometry = configured_geometry(volume_only);
        const BrandAudit audit = audit_brand(m, curves, Method::Robot, legacy, adaptive, &costs);
        const QString csv = audit.to_csv();
        out << (header_written ? csv.section('\n', 1) : csv);
        header_written = true;
    }
    if (!matched) {
        err << "no brand matching \"" << want << "\". Known brands:\n";
        for (const Menu &m : loaded.menus) err << "  " << m.brand << "\n";
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
    QString error;
    if (!curves.load_csv(asset_dir + "/data/mass_to_volume.csv", &error)) {
        err << "curves: " << error << "\n";
        return 2;
    }
    CostTable costs;
    costs.load_csv(asset_dir + "/data/ingredient_costs.csv");

    const LoadResult loaded = load_configured_menus(asset_dir);
    std::vector<const Menu *> menus;
    for (const Menu &m : loaded.menus)
        if (want.compare("all", Qt::CaseInsensitive) == 0
            || m.brand.compare(want, Qt::CaseInsensitive) == 0)
            menus.push_back(&m);
    if (menus.empty()) {
        err << "no brand matching \"" << want << "\". Known brands:\n";
        for (const Menu &m : loaded.menus) err << "  " << m.brand << "\n";
        return 2;
    }

    SweepLimits limits;
    if (max_toppings >= 0) limits.max_toppings = max_toppings;
    if (max_proteins >= 0) limits.max_proteins = max_proteins;
    if (sauces > 0) limits.min_sauces = limits.max_sauces = sauces;

    QFile file(out_path);
    const bool to_stdout = out_path.isEmpty() || out_path == "-";
    if (!to_stdout && !file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        err << "cannot write " << out_path << "\n";
        return 2;
    }
    QTextStream out(stdout);
    if (!to_stdout) out.setDevice(&file);

    qint64 last_pct = -1;
    std::vector<SweepTotals> totals;
    SimSettings legacy;
    AdaptiveSettings adaptive;
    legacy.geometry = adaptive.geometry = configured_geometry(volume_only);
    run_sweep(menus, curves, Method::Robot, legacy, adaptive, &costs,
              limits, out, [&](qint64 done, qint64 total) {
                  const qint64 pct = total > 0 ? done * 100 / total : 100;
                  if (pct != last_pct) {
                      last_pct = pct;
                      err << "\r" << pct << "%" << Qt::flush;
                  }
                  return true;
              },
              &totals);
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
    }
    err << "\r" << all.rows << " bowls; legacy: " << all.legacy_fits << " right, "
        << all.legacy_over << " over the bowl, " << all.legacy_short
        << " short of the floor. Adaptive: " << all.adaptive_over_bowl
        << " still do not fit, "
        << all.adaptive_short << " cannot reach the fill target.\n";
    return 0;
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

    QApplication app(argc, argv);
    app.setApplicationName("Bowl Fill Simulator");
    app.setOrganizationName("Lab37");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Simulates the dynamic portion algorithm against a bowl's volume limit.");
    parser.addHelpOption();
    QCommandLineOption assets(
        {"a", "assets"},
        "Directory holding menus/, data/ and photos/ (default: the build's source dir).",
        "dir", BOWLFILL_ASSET_DIR);
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
    QCommandLineOption mode("mode", "Dispense model: legacy or adaptive.", "m");
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
    parser.process(app);

    if (parser.isSet(sweep))
        return run_sweep_cli(QDir(parser.value(assets)).absolutePath(), parser.value(sweep),
                             parser.value(sweep_out),
                             parser.isSet(max_top) ? parser.value(max_top).toInt() : -1,
                             parser.isSet(max_pro) ? parser.value(max_pro).toInt() : -1,
                             parser.isSet(sauces) ? parser.value(sauces).toInt() : -1,
                             parser.isSet(volume_only));

    if (parser.isSet(audit))
        return run_audit(QDir(parser.value(assets)).absolutePath(), parser.value(audit),
                         parser.isSet(volume_only));

    bowlfill::theme::follow_system();
    if (parser.isSet(dark)) bowlfill::theme::set_dark(true);
    if (parser.isSet(unit))
        bowlfill::units::set_from_cli(parser.value(unit).startsWith("m")
                                 ? bowlfill::units::Volume::Millilitres
                                 : bowlfill::units::Volume::FluidOunces);

    bowlfill::MainWindow window(QDir(parser.value(assets)).absolutePath());
    if (parser.isSet(mode) && parser.value(mode).startsWith("a")) window.select_adaptive();
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
