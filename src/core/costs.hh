#pragma once

#include "menu_model.hh"
#include "simulator.hh"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bowlfill
{

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

} // namespace bowlfill
