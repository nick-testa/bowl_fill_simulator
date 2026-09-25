#include "ui/main_window.hh"
#include "ui/theme.hh"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

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
    QCommandLineOption scale("scale",
                             "Render at this device pixel ratio, e.g. 2 for a sharp "
                             "screenshot. Applied before the window is built.", "n");
    parser.addOption(scale);
    QCommandLineOption size("size", "Window size as WxH, e.g. 1400x1600.", "WxH");
    parser.addOption(size);
    QCommandLineOption pick("select",
                            "Preselect \"Brand|Recipe\" before rendering.", "spec");
    parser.addOption(pick);
    parser.process(app);

    bowlfill::theme::follow_system();
    if (parser.isSet(dark)) bowlfill::theme::set_dark(true);

    bowlfill::MainWindow window(QDir(parser.value(assets)).absolutePath());
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
