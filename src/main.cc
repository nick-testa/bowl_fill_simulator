#include "core/feasibility.hh"
#include "ui/main_window.hh"
#include "ui/theme.hh"
#include "ui/units.hh"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTextStream>
#include <QTimer>

namespace {

/// Headless feasibility audit, so the culinary team's report can be regenerated in a
/// script without opening the window.
int run_audit(const QString &asset_dir, const QString &want)
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

    const LoadResult loaded = load_menus(asset_dir + "/menus");
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
        const BrandAudit audit =
            audit_brand(m, curves, Method::Robot, SimSettings{}, AdaptiveSettings{}, &costs);
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
    parser.process(app);

    if (parser.isSet(audit))
        return run_audit(QDir(parser.value(assets)).absolutePath(), parser.value(audit));

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
