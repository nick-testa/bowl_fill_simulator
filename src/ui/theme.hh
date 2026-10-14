#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QAbstractSpinBox;

namespace bowlfill::theme {

///
/// The palette the HTML simulator uses, carried over so the two read as one tool.
/// Light and dark are separate steps rather than an automatic inversion, and the
/// three series hues pass CVD separation checks against both surfaces.
///
struct Palette {
    QColor ground, panel, panel_sunk;
    QColor ink, ink_soft, ink_faint;
    QColor rule, rule_strong;
    QColor accent, accent_soft;
    QColor over, over_soft;
    QColor mass;
    QColor romaine, kale, rice;
};

const Palette &palette();
void set_dark(bool dark);
bool is_dark();

/// Follows the desktop colour scheme unless the user has overridden it.
void follow_system();

///
/// The type scale. Every piece of text in the app, whether styled by the stylesheet
/// or painted by a chart, takes one of these four sizes, so nothing drifts. Sizes are
/// in pixels on both paths; mixing points (QPainter) with pixels (stylesheets) is what
/// made the charts read a size apart from the widgets around them.
///
enum class Text {
    Caption,   ///< 12 px: axis ticks, table headers, field labels, help text
    Body,      ///< 14 px: controls, table cells, legends
    Heading,   ///< 16 px: panel titles, the verdict
    Display,   ///< 24 px: the window title, stat values
};

int pixel_size(Text role);
QFont font(Text role, bool strong = false);

/// Monospace, for code-like strings only (menu keys, formulas). Numbers use the
/// sans face, whose figures are already tabular.
QFont mono(Text role = Text::Caption);

/// Spacing scale, in pixels. Layout margins and gaps use these and nothing else.
namespace space {
constexpr int xs = 4, sm = 8, md = 12, lg = 16, xl = 24;
}

/// Series colour for a base ingredient, falling back to the accent for anything
/// without an assigned hue.
QColor series_colour(const QString &ingredient);

///
/// Roughly what an ingredient looks like in the bowl, from words in its name: romaine
/// green, Mexican rice orange, black beans near-black, feta white. The bowl diagram and
/// its table use it so a chef can match a band to the food at a glance. `kind` picks a
/// fallback for names with no match: 0 base, 1 protein, 2 topping.
///
QColor food_colour(const QString &ingredient, int kind);

///
/// Makes a spin box a plain typed field: no arrow buttons, text inset like a line
/// edit. Use it rather than setButtonSymbols(NoButtons): Qt 6.12's stylesheet style
/// gives a NoButtons spin box a 1 px edit field, so it shows nothing. The arrows are
/// kept but drawn zero-wide (the "bare" rule in stylesheet()).
///
void bare_spin(QAbstractSpinBox *spin);
/// Insets a spin box's text the way the stylesheet's padding does for a line edit,
/// which Qt 6.12 no longer applies inside a spin box.
void inset_spin(QAbstractSpinBox *spin);

/// A Qt stylesheet for the whole window, rebuilt whenever the palette changes.
QString stylesheet();

}  // namespace bowlfill::theme
