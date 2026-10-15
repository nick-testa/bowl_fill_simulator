#pragma once

#include "curves.hh"
#include "menu_model.hh"
#include "simulator.hh"

#include <string>
#include <vector>

namespace bowlfill
{

///
/// Volume Fill as lab37 runs it: lab37/orders/volume_fill_algorithm.hh reads its
/// curves and settings from a generated source file, volume_fill_curves.cc. This
/// writes that file from the menus, curves and settings the simulator uses, so the
/// robot fills bowls the way the simulator predicts them.
///
/// Production names a customization by the ingredient name exactly as the menu
/// mapping spells it, so each row is keyed by brand and that spelling, not the
/// cleaned or mapped name. The brand is the simulator's name for it ("ORO"), which
/// the menu mapping's volume_fill_brand names.
///
struct VolumeFillExport
{
    std::string source; ///< volume_fill_curves.cc
    int rows = 0;
};

VolumeFillExport export_volume_fill(const std::vector<Menu> &menus,
                                    const CurveSet &curves,
                                    Method method,
                                    const SimSettings &settings,
                                    const VolumeFillSettings &vf);

} // namespace bowlfill
