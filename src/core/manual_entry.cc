#include "manual_entry.hh"

#include <algorithm>
#include <map>
#include <utility>

namespace bowlfill
{
namespace
{

bool is_manual(const Menu &m)
{
    return m.brand == kManualEntryBrand;
}

Kind kind_from(const std::string &kind)
{
    return kind == "base" ? Kind::Base : kind == "protein" ? Kind::Protein : Kind::Topping;
}

double median(std::vector<double> v)
{
    if (v.empty())
    {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

///
/// What a written-in ingredient of `kind` takes: the median of every brand's settings
/// for that kind, from ingredients whose portions came from a menu rather than a guess.
///
Ingredient typical(const std::vector<Menu> &menus, Kind kind)
{
    std::vector<double> portion, step, max, minimum;
    for (const Menu &m : menus)
    {
        if (is_manual(m))
        {
            continue;
        }
        for (const auto &[name, ing] : m.ingredients)
        {
            if (ing.kind != kind || ing.source > WeightSource::Template || ing.full_portion() <= 0)
            {
                continue;
            }
            portion.push_back(ing.full_portion());
            step.push_back(ing.step_increment_g);
            max.push_back(ing.max_dispense_weight_g);
            minimum.push_back(ing.per_portion_minimum_weight_g);
        }
    }
    Ingredient out;
    out.kind = kind;
    out.source = WeightSource::Class;
    // A catalogue with nothing of this kind still needs a portion to start from.
    const double fallback = kind == Kind::Base ? 200 : kind == Kind::Protein ? 110 : 40;
    const double full = portion.empty() ? fallback : median(portion);
    out.weights = {full};
    out.step_increment_g = median(step);
    out.max_dispense_weight_g = std::max(full, median(max));
    out.per_portion_minimum_weight_g = median(minimum);
    return out;
}

} // namespace

bool ManualEntry::empty() const
{
    return sauces.empty() &&
           std::none_of(stations.begin(), stations.end(), [](const ManualStation &s) { return !s.name.empty(); });
}

std::vector<std::string> ingredient_catalog(const std::vector<Menu> &menus)
{
    std::vector<std::string> out;
    for (const Menu &m : menus)
    {
        if (is_manual(m))
        {
            continue;
        }
        for (const auto &[name, ing] : m.ingredients)
        {
            out.push_back(name);
        }
        out.insert(out.end(), m.sauces.begin(), m.sauces.end());
    }
    remove_duplicates(out);
    sort_case_insensitive(out);
    return out;
}

std::vector<std::string> sauce_catalog(const std::vector<Menu> &menus)
{
    std::vector<std::string> out;
    for (const Menu &m : menus)
    {
        if (!is_manual(m))
        {
            out.insert(out.end(), m.sauces.begin(), m.sauces.end());
        }
    }
    remove_duplicates(out);
    sort_case_insensitive(out);
    return out;
}

std::string catalog_kind(const std::vector<Menu> &menus, const std::string &name)
{
    std::map<std::string, int> votes;
    for (const Menu &m : menus)
    {
        if (is_manual(m))
        {
            continue;
        }
        if (m.is_sauce(name))
        {
            ++votes["sauce"];
        }
        else if (const Ingredient *ing = m.find(name))
        {
            ++votes[to_lower(to_string(ing->kind))];
        }
    }
    if (votes.empty())
    {
        return is_sauce_name(name) ? std::string("sauce") : to_lower(to_string(guess_kind(name)));
    }
    return std::max_element(
               votes.begin(), votes.end(), [](const auto &a, const auto &b) { return a.second < b.second; })
        ->first;
}

Menu manual_entry_menu(const std::vector<Menu> &menus, const ManualEntry &entry, const PieceHeights *pieces)
{
    Menu menu;
    menu.brand = kManualEntryBrand;
    menu.version = "robot stations";
    // The robot splits two bases 50:50 whatever the menu says.
    menu.has_split_rule = true;

    std::map<Kind, Ingredient> typical_of;
    for (const ManualStation &s : entry.stations)
    {
        const std::string name = trim(s.name);
        if (name.empty() || contains(menu.order, name) || contains(menu.sauces, name))
        {
            continue;
        }
        const std::string kind = contains(kMappingKinds, s.kind) ? s.kind : catalog_kind(menus, name);
        if (kind == "sauce")
        {
            menu.sauces.push_back(name);
            continue;
        }

        // A brand that ramps it, then the one whose portion is best established,
        // first brand on a tie. Some brands list an ingredient with no ramp at all.
        auto rank = [](const Ingredient *i) {
            const bool ramps = i->step_increment_g > 0 && i->max_dispense_weight_g > 0;
            return std::pair{!ramps, static_cast<int>(i->source)};
        };
        const Ingredient *best = nullptr;
        for (const Menu &m : menus)
        {
            if (const Ingredient *ing = m.find(name); ing && !is_manual(m) && (!best || rank(ing) < rank(best)))
            {
                best = ing;
            }
        }

        Ingredient ing;
        if (best)
        {
            ing = *best;
        }
        else
        {
            const Kind k = kind_from(kind);
            if (!typical_of.count(k))
            {
                typical_of[k] = typical(menus, k);
            }
            ing = typical_of[k];
            if (auto it = pieces ? pieces->find(name) : PieceHeights::const_iterator{}; pieces && it != pieces->end())
            {
                ing.piece_height_mm = std::max(0.0, it->second);
            }
            else if (k == Kind::Protein)
            {
                ing.piece_height_mm = kPlaceholderProteinPieceMm;
                ing.piece_height_placeholder = true;
            }
        }
        ing.name = name;
        ing.kind = kind_from(kind);
        menu.ingredients[name] = ing;
        menu.order.push_back(name);
    }
    for (const std::string &raw : entry.sauces)
    {
        const std::string name = trim(raw);
        if (!name.empty())
        {
            append_unique(menu.sauces, name);
        }
    }

    if (entry.floor_g > 0)
    {
        FloorRule all; // no must-have: every bowl
        all.minimum_product_weight_g = entry.floor_g;
        menu.floors.push_back(all);
    }

    // One customer-built shell, so the brand report and the full sweep have a recipe
    // to run every combination of stations through.
    Recipe shell;
    shell.name = "Build your own";
    shell.customer_built = true;
    menu.recipes.push_back(shell);
    return menu;
}

} // namespace bowlfill
