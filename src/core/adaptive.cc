#include "adaptive.hh"
#include "curves.hh"

#include "text.hh"

#include <algorithm>
#include <cmath>

namespace bowlfill
{
namespace
{

/// Volume of one ingredient at a given mass, using whatever the BowlItem carries:
/// a fitted power law for the measured bases, a flat rate for everything else.
double volume_at(const BowlItem &it, double grams)
{
    return it.volume_oz(grams);
}

} // namespace

double Tolerances::for_kind(Kind k) const
{
    switch (k)
    {
    case Kind::Base:
        return base;
    case Kind::Protein:
        return protein;
    case Kind::Topping:
        return topping;
    }
    return topping;
}

double AdaptiveSettings::sauce_volume_oz() const
{
    return bowl_capacity_oz - food_capacity_oz();
}

double AdaptiveSettings::food_capacity_oz() const
{
    return bowlfill::food_capacity_oz(bowl_capacity_oz, sauce, geometry, chunk_height_mm);
}

std::string to_string(AdaptiveStatus s)
{
    switch (s)
    {
    case AdaptiveStatus::NoAdjustment:
        return "no adjustment";
    case AdaptiveStatus::Adjusted:
        return "adjusted";
    case AdaptiveStatus::Underfilled:
        return "underfilled";
    case AdaptiveStatus::StillOver:
        return "still over at max reduction";
    case AdaptiveStatus::NoIngredients:
        return "empty";
    }
    return {};
}

double AdaptiveItem::delta_pct() const
{
    return nominal_g > 0 ? (final_g / nominal_g - 1.0) * 100.0 : 0.0;
}

double AdaptiveItem::band_used(const Tolerances &t) const
{
    const double tol = t.for_kind(kind);
    if (tol <= 0 || nominal_g <= 0)
    {
        return 0.0;
    }
    return std::clamp((final_g / nominal_g - 1.0) / tol, -1.0, 1.0);
}

double AdaptiveResult::food_fill() const
{
    const double room = settings.food_capacity_oz();
    return room > 0 ? food_volume_oz / room : 0.0;
}

double AdaptiveResult::occupancy() const
{
    return settings.bowl_capacity_oz > 0 ? total_volume_oz / settings.bowl_capacity_oz : 0.0;
}

double AdaptiveResult::cogs_ratio() const
{
    return settings.menu_price > 0 ? total_cost / settings.menu_price : 0.0;
}

bool AdaptiveResult::cogs_within_guardrail() const
{
    return settings.menu_price <= 0 || cogs_ratio() <= settings.cogs_target;
}

//
// ############################################################################
//

bool CostTable::load_csv(const std::string &path, std::string *error)
{
    std::vector<std::string> lines;
    if (!read_lines(path, lines))
    {
        if (error)
        {
            *error = "could not open " + path;
        }
        return false;
    }
    per_kg_.clear();
    fallback_.clear();

    bool seen_header = false;
    for (const std::string &raw_line : lines)
    {
        const std::string line = trim(raw_line);
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const std::vector<std::string> cells = split(line, ',');
        if (cells.size() < 2)
        {
            continue;
        }
        const std::string name = trim(cells[0]);
        if (!seen_header && iequals(name, "ingredient"))
        {
            seen_header = true;
            continue;
        }
        const std::optional<double> per_kg = parse_double(cells[1]);
        if (!per_kg || *per_kg < 0)
        {
            continue;
        }

        // "*base" / "*protein" / "*topping" set the per-kind fallback.
        if (name.starts_with('*'))
        {
            const std::string kind = to_lower(name.substr(1));
            if (kind == "base")
            {
                fallback_[Kind::Base] = *per_kg;
            }
            else if (kind == "protein")
            {
                fallback_[Kind::Protein] = *per_kg;
            }
            else if (kind == "topping")
            {
                fallback_[Kind::Topping] = *per_kg;
            }
            continue;
        }
        per_kg_[name] = *per_kg;
    }
    if (per_kg_.empty() && error)
    {
        *error = "no usable rows";
    }
    return !per_kg_.empty();
}

double CostTable::cost_of(const std::string &name, Kind kind, double grams, bool *known) const
{
    auto it = per_kg_.find(name);
    if (it != per_kg_.end())
    {
        if (known)
        {
            *known = true;
        }
        return grams / 1000.0 * it->second;
    }
    if (known)
    {
        *known = false;
    }
    auto f = fallback_.find(kind);
    return f == fallback_.end() ? 0.0 : grams / 1000.0 * f->second;
}

//
// ############################################################################
//

std::vector<AdaptiveItem> nominal_from_items(const std::vector<BowlItem> &items)
{
    std::vector<AdaptiveItem> out;
    for (const BowlItem &it : items)
    {
        AdaptiveItem a;
        a.name = it.name;
        a.kind = it.kind;
        a.nominal_g = it.start_g;
        a.final_g = it.start_g;
        // The ramp's max is a real hardware/config limit and still applies. Without a
        // configured minimum, allow the tolerance band to be the only lower bound.
        a.max_g = it.max_g > 0 ? it.max_g : it.start_g;
        a.min_g = 0.0;
        out.push_back(a);
    }
    return out;
}

AdaptiveResult solve_adaptive(const std::vector<BowlItem> &items,
                              const AdaptiveSettings &settings,
                              const CostTable *costs)
{
    AdaptiveResult r;
    r.settings = settings;
    r.settings.chunk_height_mm = tallest_chunk_mm(items);
    r.items = nominal_from_items(items);
    if (r.items.empty())
    {
        return r;
    }

    const Tolerances &tol = settings.tolerances;

    // Mass of ingredient i at adjustment a, bounded by its tolerance band and then
    // clamped to its configured limits.
    auto mass_at = [&](size_t i, double a) {
        const AdaptiveItem &it = r.items[i];
        const double t = tol.for_kind(it.kind);
        return std::clamp(it.nominal_g * (1.0 + a * t), it.min_g, it.max_g);
    };
    auto volume_at_alpha = [&](double a) {
        double v = 0.0;
        for (size_t i = 0; i < r.items.size(); ++i)
        {
            v += volume_at(items[i], mass_at(i, a));
        }
        return v;
    };

    r.nominal_volume_oz = volume_at_alpha(0.0);
    // The target is a share of the food room: the fill line less what the cups and
    // any tall chunks take, so cups in the bowl leave less for food to aim at.
    const double cap = r.settings.food_capacity_oz();
    const double food_target = settings.target_fill * cap;

    double alpha = 0.0;
    const double nominal_fill = cap > 0 ? r.nominal_volume_oz / cap : 0.0;

    if (nominal_fill >= settings.band_low && nominal_fill <= settings.band_high)
    {
        // Scenario 1: already inside the acceptable visual range, so leave it alone.
        r.status = AdaptiveStatus::NoAdjustment;
    }
    else
    {
        const double v_min = volume_at_alpha(-1.0);
        const double v_max = volume_at_alpha(1.0);
        if (v_min > food_target)
        {
            // Cannot come down far enough even at full reduction. Favour underfilling
            // (US-5), so take the smallest bowl available.
            alpha = -1.0;
            r.status = AdaptiveStatus::StillOver;
        }
        else if (v_max < food_target)
        {
            alpha = 1.0;
            r.status = AdaptiveStatus::Underfilled;
        }
        else
        {
            // Volume is monotone non-decreasing in alpha, so bisection converges.
            double lo = -1.0, hi = 1.0;
            for (int i = 0; i < 60; ++i)
            {
                const double mid = (lo + hi) / 2;
                (volume_at_alpha(mid) < food_target ? lo : hi) = mid;
            }
            alpha = (lo + hi) / 2;
            r.status = AdaptiveStatus::Adjusted;
        }
    }

    r.alpha = alpha;
    r.cost_complete = costs != nullptr;
    for (size_t i = 0; i < r.items.size(); ++i)
    {
        AdaptiveItem &it = r.items[i];
        const double unclamped = it.nominal_g * (1.0 + alpha * tol.for_kind(it.kind));
        it.final_g = mass_at(i, alpha);
        it.clamped = std::fabs(it.final_g - unclamped) > 1e-6;
        it.volume_oz = volume_at(items[i], it.final_g);
        if (costs)
        {
            bool known = false;
            it.cost = costs->cost_of(it.name, it.kind, it.final_g, &known);
            it.cost_known = known;
            if (!known)
            {
                r.cost_complete = false;
            }
        }
        r.food_volume_oz += it.volume_oz;
        r.total_cost += it.cost;
    }
    r.total_volume_oz = r.food_volume_oz + r.settings.sauce_volume_oz();
    std::vector<double> volumes;
    for (const AdaptiveItem &it : r.items)
    {
        volumes.push_back(it.volume_oz);
    }
    r.fill = fill_uncertainty(items, volumes, r.settings.food_capacity_oz(), r.settings.rim_oz(), &r.settings.dispense);
    return r;
}

} // namespace bowlfill
