#include "theme.hh"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>

#include <algorithm>
#include <utility>

namespace bowlfill::theme {
namespace {

const Palette kLight = {
    QColor("#F6F6F3"), QColor("#FFFFFF"), QColor("#EFEFEA"),
    QColor("#1A1E1C"), QColor("#5A615D"), QColor("#8A918C"),
    QColor("#DDDED8"), QColor("#C3C5BC"),
    QColor("#0E6E6E"), QColor("#D6E8E6"),
    QColor("#B3452F"), QColor("#F3DED8"),
    QColor("#6A5AA8"),
    QColor("#2E8B3D"), QColor("#7A5AD6"), QColor("#C87418"),
};

const Palette kDark = {
    QColor("#131614"), QColor("#1B1F1D"), QColor("#232825"),
    QColor("#E8EAE6"), QColor("#A2AAA4"), QColor("#757D77"),
    QColor("#2C322E"), QColor("#3D453F"),
    QColor("#4FB5AE"), QColor("#1D3736"),
    QColor("#E0785C"), QColor("#3A211A"),
    QColor("#A395DD"),
    QColor("#4FA855"), QColor("#9478E4"), QColor("#CC8226"),
};

bool g_dark = false;

/// Picks the first installed family from a preference list so the app looks right
/// whether or not the IBM Plex / Archivo families the HTML version uses are present.
QString first_available(const QStringList &families, const QString &fallback)
{
    const QStringList installed = QFontDatabase::families();
    for (const QString &f : families)
        if (installed.contains(f, Qt::CaseInsensitive)) return f;
    return fallback;
}

}  // namespace

const Palette &palette() { return g_dark ? kDark : kLight; }
void set_dark(bool dark) { g_dark = dark; }
bool is_dark() { return g_dark; }

void follow_system()
{
    // Qt::ColorScheme only arrived in 6.5; judging the default palette's window
    // lightness works across versions and is what the style is actually using.
    const QColor window = QApplication::palette().color(QPalette::Window);
    g_dark = window.lightness() < 128;
}

QFont mono(int point_size, int weight)
{
    static const QString family = first_available(
        {"IBM Plex Mono", "JetBrains Mono", "DejaVu Sans Mono", "Liberation Mono"},
        QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    QFont f(family, point_size);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QFont sans(int point_size, int weight)
{
    static const QString family = first_available(
        {"IBM Plex Sans", "Inter", "Noto Sans", "DejaVu Sans"},
        QApplication::font().family());
    QFont f(family, point_size);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QFont display(int point_size)
{
    static const QString family = first_available(
        {"Archivo", "IBM Plex Sans", "Inter", "Noto Sans"}, QApplication::font().family());
    QFont f(family, point_size);
    f.setWeight(QFont::Bold);
    return f;
}

QColor series_colour(const QString &ingredient)
{
    const Palette &p = palette();
    if (ingredient == "Romaine Base") return p.romaine;
    if (ingredient == "Massaged Kale") return p.kale;
    if (ingredient == "Mexican Rice" || ingredient == "White Rice"
        || ingredient == "Brown Rice and Lentils")
        return p.rice;
    return p.accent;
}

QString stylesheet()
{
    const Palette &p = palette();
    // Named substitution rather than QString::arg: arg() fills the lowest-numbered
    // placeholder remaining, which silently misassigns when the list is long.
    const std::pair<const char *, QString> vars[] = {
        {"@ground", p.ground.name()},       {"@panel", p.panel.name()},
        {"@sunk", p.panel_sunk.name()},     {"@ink", p.ink.name()},
        {"@inkSoft", p.ink_soft.name()},    {"@inkFaint", p.ink_faint.name()},
        {"@rule", p.rule.name()},           {"@ruleStrong", p.rule_strong.name()},
        {"@accent", p.accent.name()},       {"@accentSoft", p.accent_soft.name()},
        {"@over", p.over.name()},
        {"@sans", sans().family()},         {"@mono", mono().family()},
        {"@display", display().family()},
    };

    QString css = R"(
QWidget { background: @ground; color: @ink; font-family: "@sans"; font-size: 13px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: @ground; }
/* Labels and check boxes inherit the panel they sit in rather than painting the
   window colour as a box behind themselves. */
QLabel, QCheckBox { background: transparent; }
/* Layout-only wrappers must not paint the window colour inside a panel. */
QWidget#clear { background: transparent; }

QFrame#panel { background: @panel; border: 1px solid @rule; border-radius: 10px; }
QFrame#card  { background: @panel; border: 1px solid @rule; border-radius: 8px; }
QFrame#sunk  { background: @sunk;  border: 1px solid @rule; border-radius: 8px; }
QFrame#sep   { background: @rule; border: none; }

QLabel#h1 { font-family: "@display"; font-size: 22px; font-weight: 700; color: @ink; }
QLabel#h2 { font-family: "@display"; font-size: 14px; font-weight: 600; color: @ink; }
QLabel#eyebrow { font-family: "@mono"; font-size: 10px; color: @inkFaint; }
QLabel#note { color: @inkSoft; font-size: 12px; }
QLabel#warn { color: @over; font-size: 12px; }
QLabel#fieldLabel { color: @inkSoft; font-size: 11px; }
QLabel#statValue { font-family: "@mono"; font-size: 17px; font-weight: 600; color: @ink; }
QLabel#statKey { font-family: "@mono"; font-size: 9px; color: @inkFaint; }

QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
    background: @sunk; color: @ink; border: 1px solid @ruleStrong; border-radius: 6px;
    padding: 5px 8px; font-family: "@mono"; font-size: 13px;
    selection-background-color: @accent;
}
QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus, QLineEdit:focus {
    border: 1px solid @accent;
}
QComboBox { font-family: "@sans"; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: @panel; color: @ink; border: 1px solid @ruleStrong;
    selection-background-color: @accentSoft; selection-color: @ink; padding: 3px;
}
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button,
QSpinBox::up-button, QSpinBox::down-button { width: 14px; background: @sunk; border: none; }

QPushButton {
    background: @sunk; color: @ink; border: 1px solid @ruleStrong; border-radius: 6px;
    padding: 6px 12px; font-size: 13px;
}
QPushButton:hover { border-color: @accent; }
QPushButton:checked { background: @accentSoft; border-color: @accent; color: @ink; }
QPushButton#primary { background: @accentSoft; border-color: @accent; font-weight: 600; }
QPushButton#info {
    border-radius: 9px; padding: 0px; min-width: 18px; max-width: 18px;
    min-height: 18px; max-height: 18px; font-weight: 700; color: @inkSoft;
}

QCheckBox { color: @ink; font-size: 12px; spacing: 6px; padding: 4px; }
QCheckBox:disabled { color: @inkFaint; }
QCheckBox::indicator { width: 13px; height: 13px; border-radius: 3px;
                       border: 1px solid @ruleStrong; background: @sunk; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }

QHeaderView::section {
    background: @panel; color: @inkFaint; border: none; border-bottom: 1px solid @rule;
    padding: 5px 7px; font-family: "@mono"; font-size: 10px;
}
QTableWidget {
    background: @panel; gridline-color: @rule; border: none;
    font-family: "@mono"; font-size: 12px;
}
QTableWidget::item { padding: 4px 7px; }
QTableWidget::item:selected { background: @accentSoft; color: @ink; }

QToolTip { background: @panel; color: @ink; border: 1px solid @ruleStrong; padding: 8px; }
QScrollBar:vertical { background: @ground; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @ruleStrong; border-radius: 5px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QSplitter::handle { background: @rule; }
)";
    // Longest keys first so @inkSoft is not clipped by @ink.
    QList<std::pair<QString, QString>> ordered;
    for (const auto &[key, value] : vars) ordered.append({QString(key), value});
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &[key, value] : ordered) css.replace(key, value);
    return css;
}

}  // namespace bowlfill::theme
