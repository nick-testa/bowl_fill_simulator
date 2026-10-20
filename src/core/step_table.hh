#pragma once

#include "curves.hh"
#include "menu_model.hh"
#include "simulator.hh"

#include <string>
#include <vector>

namespace bowlfill
{

///
/// Volume per gram of each ingredient, for choosing step_g so that every ingredient's
/// ramp step adds about the same volume: a volume-proportional step.
///
/// A gram of romaine takes far more room than a gram of rice, so equal gram steps grow
/// a bowl unevenly. The curves are power laws (volume = K * g^p), so a gram is also worth
/// a different volume at different masses. Everything here is read at the ingredient's
/// menu portion, where its ramp starts, and unloaded: nothing is pressing on it.
///
struct StepRow
{
    std::string ingredient;
    Kind kind = Kind::Topping;
    CurveSource source = CurveSource::Flat;
    double portion_g = 0.0;

    /// The portion's volume over its mass, and the inverse. 0 when there is no portion.
    double ml_per_g = 0.0;
    double g_per_ml = 0.0;

    /// What the next gram adds at the portion. Below ml_per_g when the ingredient
    /// compacts (p < 1): each gram buys less room than the average one before it.
    double marginal_ml_per_g = 0.0;

    /// The configured step, and the volume one such step adds from the portion.
    double step_g = 0.0;
    double step_ml = 0.0;

    /// The step that would add the target volume from the portion.
    double step_g_for_target = 0.0;

    /// Its Catalog volume_curve, exactly as --export-volume-fill writes it: k in fl oz at
    /// 1 g and p (a flat rate is k = rate / 100, p = 1), for each production spelling
    /// the menu maps to this ingredient, which is what the Catalog is keyed by.
    double catalog_k = 0.0;
    double catalog_p = 1.0;
    std::vector<std::string> catalog_names;
};

/// A short name for where a curve came from: "measured", "borrowed", "composite", "flat".
std::string curve_label(CurveSource source);

/// Grams to add to `item`, from `from_g`, for its unloaded volume to grow by `ml`.
double grams_for_ml(const BowlItem &item, double from_g, double ml);

/// One row per dispensed ingredient of `menu`, bases first, then proteins, then
/// toppings, as the menu lists them. Sauces are left out: they go in cups.
std::vector<StepRow> step_table(const Menu &menu,
                                const CurveSet &curves,
                                double target_ml,
                                double flat_protein_rate = kFlatProteinRate,
                                double flat_topping_rate = kFlatToppingRate);

/// The table as CSV, one row per ingredient, with a header naming the target volume.
/// One row per production spelling: ingredient_name, kind, k, p, tab-separated, as the
/// Catalog's volume_curve fields take them.
std::string catalog_curves_tsv(const std::vector<StepRow> &rows);

std::string step_table_csv(const std::string &brand, const std::vector<StepRow> &rows, double target_ml);

} // namespace bowlfill
