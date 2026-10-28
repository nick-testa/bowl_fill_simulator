#pragma once

#include <QColor>
#include <QString>

#include <map>

namespace bowlfill {

///
/// The colour an ingredient is drawn in, where someone has chosen one in the Ingredients
/// dialog, keyed by the name in use like piece heights, so every brand that serves it
/// shares it. Stored in data/ingredient_colours.csv as ingredient,colour (#rrggbb).
/// A display preference, so it lives with the UI rather than the core.
///
using IngredientColours = std::map<QString, QColor>;

/// A missing file is not an error: nothing is chosen yet, and every ingredient keeps
/// the colour its name gives it.
bool load_ingredient_colours(const QString &path, IngredientColours &out, QString *error = nullptr);
bool save_ingredient_colours(const QString &path, const IngredientColours &colours,
                             QString *error = nullptr);

}  // namespace bowlfill
