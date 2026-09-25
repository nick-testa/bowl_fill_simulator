#pragma once

#include <QColor>
#include <QFont>
#include <QString>

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

QFont mono(int point_size = 10, int weight = QFont::Normal);
QFont sans(int point_size = 10, int weight = QFont::Normal);
QFont display(int point_size = 13);

/// Series colour for a base ingredient, falling back to the accent for anything
/// without an assigned hue.
QColor series_colour(const QString &ingredient);

/// A Qt stylesheet for the whole window, rebuilt whenever the palette changes.
QString stylesheet();

}  // namespace bowlfill::theme
