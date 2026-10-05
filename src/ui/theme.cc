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

int pixel_size(Text role)
{
    switch (role) {
    case Text::Caption: return 12;
    case Text::Body: return 14;
    case Text::Heading: return 16;
    case Text::Display: return 24;
    }
    return 14;
}

namespace {

QString sans_family()
{
    static const QString family = first_available(
        {"IBM Plex Sans", "Inter", "Noto Sans", "DejaVu Sans"},
        QApplication::font().family());
    return family;
}

QString mono_family()
{
    static const QString family = first_available(
        {"IBM Plex Mono", "JetBrains Mono", "DejaVu Sans Mono", "Liberation Mono"},
        QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    return family;
}

}  // namespace

QFont font(Text role, bool strong)
{
    QFont f(sans_family());
    f.setPixelSize(pixel_size(role));
    f.setWeight(strong ? QFont::DemiBold : QFont::Normal);
    return f;
}

QFont mono(Text role)
{
    QFont f(mono_family());
    f.setPixelSize(pixel_size(role));
    return f;
}

QColor series_colour(const QString &ingredient)
{
    const Palette &p = palette();
    if (ingredient == "Romaine Base") return p.romaine;
    if (ingredient == "Massaged Kale") return p.kale;
    if (ingredient.contains("rice", Qt::CaseInsensitive)) return p.rice;
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
        {"@overSoft", p.over_soft.name()},  {"@accentHover", p.accent.lighter(112).name()},
        {"@sans", sans_family()},
        {"@captionpx", QString("%1px").arg(pixel_size(Text::Caption))},
        {"@bodypx", QString("%1px").arg(pixel_size(Text::Body))},
        {"@headingpx", QString("%1px").arg(pixel_size(Text::Heading))},
        {"@displaypx", QString("%1px").arg(pixel_size(Text::Display))},
    };

    // Four text sizes (@caption, @body, @heading, @display) and two weights (400 and
    // 600) carry the whole hierarchy; colour (@ink, @inkSoft, @inkFaint) does the rest.
    // Spacing sticks to multiples of 4 px.
    QString css = R"(
QWidget { background: @ground; color: @ink; font-family: "@sans"; font-size: @bodypx; }
QScrollArea, QScrollArea > QWidget > QWidget { background: @ground; border: none; }
QLabel, QCheckBox, QRadioButton { background: transparent; }
QWidget#clear { background: transparent; }

QFrame#panel { background: @panel; border: 1px solid @rule; border-radius: 10px; }
QFrame#sunk  { background: @sunk;  border: 1px solid @rule; border-radius: 8px; }
QFrame#sep   { background: @rule; border: none; }

/* ---- text roles ------------------------------------------------------------ */
QLabel#title    { font-size: @displaypx; font-weight: 600; }
QLabel#subtitle { font-size: @captionpx; color: @inkSoft; }
QLabel#heading  { font-size: @headingpx; font-weight: 600; }
QLabel#section  { font-size: @captionpx; font-weight: 600; color: @inkFaint; }
QLabel#label    { font-size: @captionpx; color: @inkSoft; }
QLabel#note     { font-size: @captionpx; color: @inkSoft; }
QLabel#lead     { color: @inkSoft; }
QFrame#stat      { background: transparent; border: none; border-left: 1px solid @rule; }
QFrame#statFirst { background: transparent; border: none; }
QLabel#statKey   { font-size: @captionpx; color: @inkSoft; }
QLabel#statValue { font-size: @displaypx; font-weight: 600; }

/* ---- inputs ----------------------------------------------------------------- */
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
    background: @sunk; color: @ink; border: 1px solid @rule; border-radius: 6px;
    padding: 0px 8px; font-size: @bodypx;
    selection-background-color: @accent;
}
QLineEdit:hover, QDoubleSpinBox:hover, QSpinBox:hover, QComboBox:hover {
    border-color: @ruleStrong;
}
QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus, QLineEdit:focus {
    border-color: @accent;
}
QComboBox, QLineEdit { min-height: 30px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
    background: @panel; color: @ink; border: 1px solid @ruleStrong; outline: none;
    selection-background-color: @accentSoft; selection-color: @ink; padding: 4px;
}

/* ---- buttons ---------------------------------------------------------------- */
QPushButton, QToolButton {
    background: @sunk; color: @ink; border: 1px solid @rule; border-radius: 6px;
    padding: 0px 12px; min-height: 30px; font-size: @bodypx;
}
QPushButton:hover, QToolButton:hover { border-color: @ruleStrong; }
QPushButton#primary { background: @accent; color: @panel; border-color: @accent;
                      font-weight: 600; }
QPushButton#primary:hover { background: @accentHover; border-color: @accentHover; }
QPushButton#quiet { background: transparent; border-color: transparent; color: @inkSoft; }
QPushButton#quiet:hover { color: @ink; background: @sunk; }
QPushButton#issues { background: @overSoft; border-color: @over; color: @over;
                     font-weight: 600; }

/* Segmented control: a row of checkable buttons sharing one outline. */
QFrame#segment { background: @sunk; border: 1px solid @rule; border-radius: 7px; }
QFrame#segment QPushButton {
    background: transparent; border: none; border-radius: 5px; min-height: 26px;
    padding: 0px 12px; color: @inkSoft;
}
QFrame#segment QPushButton:hover { color: @ink; }
QFrame#segment QPushButton:checked { background: @panel; color: @ink; font-weight: 600;
                                     border: 1px solid @rule; }

/* Collapsible section header in the sidebar. */
QToolButton#disclosure {
    background: transparent; border: none; padding: 0px; min-height: 24px;
    font-size: @captionpx; font-weight: 600; color: @inkFaint; text-align: left;
}
QToolButton#disclosure:hover { color: @ink; }

QCheckBox { spacing: 8px; padding: 4px 0px; }
QCheckBox:disabled { color: @inkFaint; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px;
                       border: 1px solid @ruleStrong; background: @sunk; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }

/* ---- tables ----------------------------------------------------------------- */
QHeaderView { background: transparent; border: none; }
QHeaderView::section {
    background: @panel; color: @inkSoft; border: none; border-bottom: 1px solid @rule;
    padding: 8px; font-size: @captionpx; font-weight: 600;
}
QTableWidget {
    background: @panel; border: none; font-size: @bodypx; outline: none;
    selection-background-color: @accentSoft; selection-color: @ink;
}
QTableWidget::item { padding: 0px 8px; border-bottom: 1px solid @rule; }
QTableWidget QDoubleSpinBox { min-height: 26px; border-radius: 4px; }

QToolTip { background: @panel; color: @ink; border: 1px solid @ruleStrong;
           padding: 8px; font-size: @captionpx; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @ruleStrong; border-radius: 3px; min-height: 32px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @ruleStrong; border-radius: 3px; min-width: 32px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: transparent; width: 12px; }
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
