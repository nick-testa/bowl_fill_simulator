#pragma once

#include "menu_model.hh"
#include "simulator.hh"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bowlfill
{

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

struct Tolerances
{
    double base = 0.20;
    double protein = 0.05;
    double topping = 0.10;

    double for_kind(Kind k) const;
};

struct AdaptiveSettings
{
    double bowl_capacity_oz = 32.0;

    /// Target for the FOOD, as a share of the food room (food_capacity_oz()).
    double target_fill = 0.85;
    /// US-1's acceptable visual range. Inside it, nothing is adjusted at all --
    /// scenario 1 of the Day in the Life is "no adjustment required".
    double band_low = 0.75;
    double band_high = 0.85;

    SauceCups sauce;
    BowlGeometry geometry;
    DispenseVariation dispense;
    /// Tallest chunk in the bowl; solve_adaptive() fills this from its items.
    double chunk_height_mm = 0.0;

    Tolerances tolerances;

    /// COGS guardrail. Reported, never solved for: the document is explicit that the
    /// system should "understand the impact on COGS, not optimize up front on COGS".
    double menu_price = 0.0; ///< 0 disables the COGS readout
    double cogs_target = 0.23;

    /// Everything that is not food: the cups' contents, or with geometry on, the room
    /// the cups and chunks need below the lid. capacity = food room + this.
    double sauce_volume_oz() const;
    /// Capacity actually available to food, once the cups are in.
    double food_capacity_oz() const;

    /// Food that reaches the rim, for the chance of going over it.
    double rim_oz() const
    {
        return rim_food_oz(sauce, geometry);
    }
};

enum class AdaptiveStatus
{
    NoAdjustment, ///< nominal already sits inside the acceptable band
    Adjusted,     ///< solved within tolerance
    Underfilled,  ///< cannot reach the target even at full positive tolerance
    StillOver,    ///< will not fit even at full negative tolerance
    NoIngredients,
};

std::string to_string(AdaptiveStatus s);

struct AdaptiveItem
{
    std::string name;
    Kind kind = Kind::Topping;
    double nominal_g = 0.0;
    double final_g = 0.0;
    double min_g = 0.0, max_g = 0.0;
    double volume_oz = 0.0;
    double cost = 0.0;
    bool clamped = false; ///< hit a min/max before its tolerance ran out
    bool cost_known = false;

    double delta_pct() const; ///< signed change against nominal
    /// How much of its allowed band this ingredient actually used, 0..1.
    double band_used(const Tolerances &t) const;
};

struct AdaptiveResult
{
    std::vector<AdaptiveItem> items;
    AdaptiveSettings settings;
    AdaptiveStatus status = AdaptiveStatus::NoIngredients;

    double alpha = 0.0;
    double nominal_volume_oz = 0.0;
    double food_volume_oz = 0.0;
    double total_volume_oz = 0.0; ///< food plus sauce cups
    double total_cost = 0.0;
    bool cost_complete = false; ///< false if any ingredient had no price
    FillUncertainty fill;       ///< the dispensed bowl as a distribution

    double food_fill() const;  ///< food as a share of the food room
    double occupancy() const;  ///< food plus cups, as a share of capacity
    double cogs_ratio() const; ///< 0 when no price is set
    bool cogs_within_guardrail() const;
};

/// Where a price came from, from the CSV's source column.
enum class CostSource
{
    KitchenOs,  ///< rolled up from the recipe by KitchenOS
    Estimate,   ///< KitchenOS has no cost; a wholesale estimate stands in
    Placeholder ///< a guess, or a row that names no source
};

std::string to_string(CostSource source);

///
/// Ingredient prices, loaded from a CSV so they can be corrected without a rebuild.
/// Falls back to a per-kind default for anything unlisted.
///
class CostTable
{
public:
    bool load_csv(const std::string &path, std::string *error = nullptr);
    /// Cost of `grams` of `name`; `known` reports whether this was a real entry
    /// rather than the per-kind fallback.
    double cost_of(const std::string &name, Kind kind, double grams, bool *known = nullptr) const;
    /// Price per kg of a listed ingredient, or nothing when it is unlisted.
    std::optional<double> per_kg(const std::string &name) const;
    /// Where a listed price came from; Placeholder for anything unlisted.
    CostSource source_of(const std::string &name) const;

    bool empty() const
    {
        return per_kg_.empty();
    }

    int size() const
    {
        return static_cast<int>(per_kg_.size());
    }

private:
    std::map<std::string, double> per_kg_;
    std::map<std::string, CostSource> source_;
    std::map<Kind, double> fallback_;
};

/// One ingredient of a costed bowl.
struct BowlCostLine
{
    std::string name;
    Kind kind = Kind::Topping;
    double grams = 0.0;
    double cost = 0.0;
    bool known = false; ///< false: priced at its kind's default
    CostSource source = CostSource::Placeholder;
};

///
/// The food cost of a finished bowl: each ingredient's final mass at its price. Sauce
/// cups are not included, since a bowl knows how many cups it carries but not which sauce.
///
struct BowlCost
{
    std::vector<BowlCostLine> lines;
    double total = 0.0;

    bool complete() const;          ///< every ingredient had its own price
    bool all_kitchenos() const;     ///< every price came from KitchenOS
    double kitchenos_share() const; ///< of the total, the part priced from KitchenOS
};

/// Costs each item at its final_g.
BowlCost cost_bowl(const std::vector<BowlItem> &items, const CostTable &costs);

/// Builds the nominal bowl a recipe specifies, in dispense order.
std::vector<AdaptiveItem> nominal_from_items(const std::vector<BowlItem> &items);

AdaptiveResult solve_adaptive(const std::vector<BowlItem> &items,
                              const AdaptiveSettings &settings,
                              const CostTable *costs = nullptr);

} // namespace bowlfill
