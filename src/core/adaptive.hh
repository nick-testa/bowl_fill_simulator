#pragma once

#include "menu_model.hh"
#include "simulator.hh"

#include <QString>
#include <map>
#include <optional>
#include <vector>

namespace bowlfill {

///
/// Adaptive Dispense, per the user stories.
///
/// A different shape of thing from the legacy ramp. The ramp iterates: it adds a flat
/// gram step to every ingredient until a *mass* floor is cleared. This solves: it
/// picks dispense quantities directly so the bowl lands on a *volume* target, with
/// each ingredient held inside a tolerance band.
///
/// The whole adjustment is one scalar. Every ingredient moves the same fraction of
/// its own allowed band:
///
///     g_i(a) = clamp( nominal_i * (1 + a * tolerance_i), min_i, max_i )
///
/// so a base with a +/-20% band moves four times as far as a protein with +/-5%.
/// That is the "4:1:2 ratio steps for base/protein/topping" from the document and the
/// tolerance table restated -- 20:5:10 reduces to 4:1:2, they are one requirement.
///
/// Total volume is monotone non-decreasing in `a`, so a is found by bisection with no
/// optimiser and no local minima.
///

struct Tolerances {
    double base = 0.20;
    double protein = 0.05;
    double topping = 0.10;

    double for_kind(Kind k) const;
};

struct AdaptiveSettings {
    double bowl_capacity_oz = 32.0;

    /// Target for the FOOD, as a share of total bowl capacity.
    double target_fill = 0.85;
    /// US-1's acceptable visual range. Inside it, nothing is adjusted at all --
    /// scenario 1 of the Day in the Life is "no adjustment required".
    double band_low = 0.75;
    double band_high = 0.85;

    SauceCups sauce;
    BowlGeometry geometry;
    /// Tallest chunk in the bowl; solve_adaptive() fills this from its items.
    double chunk_height_mm = 0.0;

    Tolerances tolerances;

    /// COGS guardrail. Reported, never solved for: the document is explicit that the
    /// system should "understand the impact on COGS, not optimize up front on COGS".
    double menu_price = 0.0;      ///< 0 disables the COGS readout
    double cogs_target = 0.23;

    /// Everything that is not food: the cups' contents, or with geometry on, the room
    /// the cups and chunks need below the lid. capacity = food room + this.
    double sauce_volume_oz() const;
    /// Capacity actually available to food, once the cups are in.
    double food_capacity_oz() const;
};

enum class AdaptiveStatus {
    NoAdjustment,     ///< nominal already sits inside the acceptable band
    Adjusted,         ///< solved within tolerance
    Underfilled,      ///< cannot reach the target even at full positive tolerance
    StillOver,        ///< will not fit even at full negative tolerance
    NoIngredients,
};

QString to_string(AdaptiveStatus s);

struct AdaptiveItem {
    QString name;
    Kind kind = Kind::Topping;
    double nominal_g = 0.0;
    double final_g = 0.0;
    double min_g = 0.0, max_g = 0.0;
    double volume_oz = 0.0;
    double cost = 0.0;
    bool clamped = false;        ///< hit a min/max before its tolerance ran out
    bool cost_known = false;

    double delta_pct() const;    ///< signed change against nominal
    /// How much of its allowed band this ingredient actually used, 0..1.
    double band_used(const Tolerances &t) const;
};

struct AdaptiveResult {
    std::vector<AdaptiveItem> items;
    AdaptiveSettings settings;
    AdaptiveStatus status = AdaptiveStatus::NoIngredients;

    double alpha = 0.0;
    double nominal_volume_oz = 0.0;
    double food_volume_oz = 0.0;
    double total_volume_oz = 0.0;   ///< food plus sauce cups
    double total_cost = 0.0;
    bool cost_complete = false;     ///< false if any ingredient had no price

    double food_fill() const;       ///< food as a share of bowl capacity
    double occupancy() const;       ///< food plus cups, as a share of capacity
    double cogs_ratio() const;      ///< 0 when no price is set
    bool cogs_within_guardrail() const;
};

///
/// Ingredient prices, loaded from a CSV so they can be corrected without a rebuild.
/// Falls back to a per-kind default for anything unlisted.
///
class CostTable {
public:
    bool load_csv(const QString &path, QString *error = nullptr);
    /// Cost of `grams` of `name`; `known` reports whether this was a real entry
    /// rather than the per-kind fallback.
    double cost_of(const QString &name, Kind kind, double grams, bool *known = nullptr) const;
    bool empty() const { return per_kg_.empty(); }
    int size() const { return static_cast<int>(per_kg_.size()); }

private:
    std::map<QString, double> per_kg_;
    std::map<Kind, double> fallback_;
};

/// Builds the nominal bowl a recipe specifies, in dispense order.
std::vector<AdaptiveItem> nominal_from_items(const std::vector<BowlItem> &items);

AdaptiveResult solve_adaptive(const std::vector<BowlItem> &items,
                              const AdaptiveSettings &settings,
                              const CostTable *costs = nullptr);

}  // namespace bowlfill
