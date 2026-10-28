#include "ingredient_colours.hh"

#include "qt_text.hh"

#include "core/text.hh"

#include <QFileInfo>

#include <string>
#include <vector>

namespace bowlfill {

bool load_ingredient_colours(const QString &path, IngredientColours &out, QString *error)
{
    if (!QFileInfo::exists(path)) return true;
    std::vector<std::string> lines;
    if (!read_lines(ss(path), lines)) {
        if (error) *error = QString("could not open %1").arg(path);
        return false;
    }
    IngredientColours read;
    int col_name = -1, col_colour = -1, line_no = 0;
    for (const std::string &raw : lines) {
        ++line_no;
        const std::string line = trim(raw);
        // A comment starts with "#". A colour does too, but only ever after the name.
        if (line.empty() || line.starts_with('#')) continue;
        const std::vector<std::string> f = split_csv(line);
        if (col_name < 0) {
            for (int i = 0; i < static_cast<int>(f.size()); ++i) {
                const std::string h = to_lower(trim(f[i]));
                if (h == "ingredient") col_name = i;
                else if (h == "colour" || h == "color") col_colour = i;
            }
            if (col_name < 0 || col_colour < 0) {
                if (error) *error = QString("%1: the header must name ingredient and colour").arg(path);
                return false;
            }
            continue;
        }
        const QString name = qs(trim(col_name < static_cast<int>(f.size()) ? f[col_name] : std::string()));
        const QString value = qs(trim(col_colour < static_cast<int>(f.size()) ? f[col_colour] : std::string()));
        if (name.isEmpty() || value.isEmpty()) continue;
        const QColor colour(value);
        if (!colour.isValid()) {
            if (error)
                *error = QString("%1, line %2: \"%3\" is not a colour (use #rrggbb)")
                             .arg(path).arg(line_no).arg(value);
            return false;
        }
        read[name] = colour;
    }
    out = std::move(read);
    return true;
}

bool save_ingredient_colours(const QString &path, const IngredientColours &colours, QString *error)
{
    std::string out = "ingredient,colour\n";
    for (const auto &[name, colour] : colours)
        out += csv_field(ss(name)) + ',' + ss(colour.name()) + '\n';
    if (!write_text(ss(path), out)) {
        if (error) *error = QString("could not write %1").arg(path);
        return false;
    }
    return true;
}

}  // namespace bowlfill
