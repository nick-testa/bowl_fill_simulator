#pragma once

#include "curves.hh"
#include "menu_model.hh"
#include "simulator.hh"

#include <string>
#include <vector>

namespace bowlfill
{

///
/// Volume Fill as lab37 runs it. lab37/orders/volume_fill_algorithm.hh reads each ingredient's
/// curve from the Catalog (RobotBrandConfig.ingredients[name].volume_curve), falling back to
/// defaults compiled from volume_fill_curves.cc. This writes both: that source file, and a
/// manifest of the values the Catalog should hold.
///
/// Production names a customization by the ingredient name exactly as the menu mapping spells
/// it, and so does the Catalog, so each row is keyed by that spelling, not the cleaned or mapped
/// name. An ingredient is the same food whichever brand serves it, so it has one curve. Where
/// brands disagree, a dispensed ingredient wins over a cupped one (the Catalog's sauces decide
/// what is cupped) and otherwise the first brand's curve is kept; both are reported.
///
struct VolumeFillRow
{
    std::string ingredient_name;
    std::string kind; ///< "Base", "Protein", "Topping" or "Sauce"
    double k = 0.0;   ///< fl oz at 1 g; a flat rate is k = rate / 100 with p = 1
    double p = 1.0;
    double piece_height_mm = 0.0;
    std::vector<std::string> brands;
};

struct VolumeFillExport
{
    std::vector<VolumeFillRow> rows;    ///< sorted by ingredient name
    std::vector<std::string> conflicts; ///< one line per spelling brands disagree on
    std::string source;                 ///< volume_fill_curves.cc
    std::string manifest;               ///< JSON for the Catalog's volume_curve fields
};

VolumeFillExport export_volume_fill(const std::vector<Menu> &menus,
                                    const CurveSet &curves,
                                    Method method,
                                    const SimSettings &settings,
                                    const VolumeFillSettings &vf);

} // namespace bowlfill
