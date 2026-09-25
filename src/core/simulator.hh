#pragma once

#include "curves.hh"
#include "menu_model.hh"

#include <QString>
#include <optional>
#include <vector>

namespace bowlfill {

///
/// One ingredient as it enters the bowl. Dispense order is also stacking order:
/// bases first, then proteins, then toppings.
///
struct BowlItem {
    QString name;
    Kind kind = Kind::Topping;
    double start_g = 0.0;
    double final_g = 0.0;  ///< filled in by simulate()
    double step_g = 0.0;
    double max_g = 0.0;

    /// Bases carry a fitted power law; everything else has no measurements and
    /// takes a flat rate.
    bool has_curve = false;
    double K = 0.0;
    double p = 1.0;
    double flat_oz_per_100g = 4.5;

    double volume_oz(double grams) const;
    double marginal_oz_per_100g(double grams) const;
};

struct Frame {
    double grams = 0.0;
    double ounces = 0.0;
    double ounces_uncompressed = 0.0;
    std::vector<double> per_item_oz;
    std::vector<double> per_item_oz_uncompressed;
};

struct SimSettings {
    double floor_g = 0.0;
    double bowl_capacity_oz = 32.0;
    bool compress = false;
    double load_transfer = 1.0;  ///< share of the weight above a base that bears on it
};

enum class Verdict {
    NoFloor,          ///< nothing ramps: no rule matched, or the menu sets no floors
    FitsMassBound,    ///< cleared the floor and stayed inside the bowl
    Saturated,        ///< every ingredient hit its max before the floor
    OverAtStart,      ///< the ordered portions alone exceed the bowl
    OverWhileRamping, ///< crossed the volume limit on the way to the floor
};

struct SimResult {
    std::vector<BowlItem> items;   ///< each carries start_g and final_g
    std::vector<Frame> frames;     ///< one per pass, frames.front() is as-ordered
    SimSettings settings;
    Verdict verdict = Verdict::NoFloor;

    int passes = 0;
    std::optional<double> crossed_at_g;    ///< mass where volume first exceeded the cap
    std::optional<double> highest_safe_floor_g;
    bool saturated = false;

    const Frame &first() const { return frames.front(); }
    const Frame &last() const { return frames.back(); }
    double grams_per_pass() const;
    double base_share_of_step() const;
    double squeezed_oz() const;
};

///
/// Mirrors maybe_apply_dynamic_portion_alogithm: every ingredient still under its
/// max takes one step per pass; stop when nothing moved or the total clears the
/// floor. The early-out is strictly greater than the floor, as in the C++.
///
SimResult simulate(std::vector<BowlItem> items, const SimSettings &settings);

///
/// apply_proportional_reduction_to_product: halve each base, then floor at its
/// configured minimum. Only fires when two bases are present.
///
double split_base_start(const Ingredient &ingredient, bool split_on, bool two_bases);

/// Builds a bowl item from menu configuration plus the fitted curves.
BowlItem make_item(const Ingredient &ingredient, const CurveSet &curves, Method method,
                   double flat_protein_rate, double flat_topping_rate);

}  // namespace bowlfill
