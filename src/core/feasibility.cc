#include "feasibility.hh"

#include "text.hh"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bowlfill
{
namespace
{

/// A template that pins only a protein is a modifier, not a bowl: the customer adds
/// the rest. Judging it as a finished bowl would report a dozen false failures.
constexpr int kCompleteBowlThreshold = 4;

} // namespace

std::vector<BowlItem> bowl_from_recipe(const Menu &menu,
                                       const Recipe &recipe,
                                       const CurveSet &curves,
                                       double flat_protein_rate,
                                       double flat_topping_rate)
{
    std::vector<BowlItem> bases, proteins, toppings;
    for (const RecipeItem &ri : recipe.items)
    {
        const Ingredient *ing = menu.find(ri.name);
        if (!ing)
        {
            continue;
        }
        BowlItem item = make_item(*ing, curves, flat_protein_rate, flat_topping_rate);
        item.start_g = ri.grams;
        (ing->kind == Kind::Base ? bases : ing->kind == Kind::Protein ? proteins : toppings).push_back(item);
    }
    std::vector<BowlItem> all;
    all.insert(all.end(), bases.begin(), bases.end());
    all.insert(all.end(), proteins.begin(), proteins.end());
    all.insert(all.end(), toppings.begin(), toppings.end());
    return all;
}

//
// ############################################################################
//

bool RecipeAudit::legacy_ok() const
{
    switch (legacy.verdict)
    {
    case Verdict::FitsMassBound:
        return true;
    case Verdict::NoFloor:
        return !legacy.crossed_at_g.has_value();
    default:
        return false;
    }
}

std::string RecipeAudit::legacy_summary() const
{
    if (customer_built)
    {
        return "Customer builds this bowl; nothing is dispensed as written.";
    }
    if (legacy.frames.empty())
    {
        return "No ingredients resolve from the menu.";
    }

    // Percentages are of the whole bowl, so the cups belong in the numerator; the
    // verdicts themselves are judged against the space left for food.
    const double cap = legacy.settings.bowl_capacity_oz;
    const double sauce = legacy.settings.overhead_oz();
    const double last = legacy.last().ounces;
    const auto pct = [&](double oz) { return std::round((oz + sauce) / cap * 100); };
    switch (legacy.verdict)
    {
    case Verdict::OverAtStart:
    {
        const double start = legacy.first().ounces;
        return std::format("Overflows as written — {} oz at the specified weights, {}% of "
                           "the bowl, and the ramp takes it to {} oz.",
                           fixed(start, 1),
                           general(pct(start)),
                           fixed(last, 1));
    }
    case Verdict::OverWhileRamping:
        return std::format("Fits as written, then the ramp pushes it over: {} oz at the "
                           "floor, crossing the limit at {} g.",
                           fixed(last, 1),
                           general(std::round(*legacy.crossed_at_g)));
    case Verdict::Saturated:
        return std::format("Never reaches its {} g floor — everything saturates at {} g, "
                           "{} oz.",
                           general(std::round(legacy.settings.floor_g)),
                           general(std::round(legacy.last().grams)),
                           fixed(last, 1));
    case Verdict::NoFloor:
        return legacy.crossed_at_g ? std::format("No floor matches this bowl, and it is over anyway at "
                                                 "{} oz.",
                                                 fixed(last, 1))
                                   : std::format("No floor matches this bowl, so the ramp never fires. "
                                                 "Dispensed as written at {} oz.",
                                                 fixed(last, 1));
    case Verdict::FitsMassBound:
        return std::format("Fits: clears the floor at {} g and {} oz, {}% of the bowl.",
                           general(std::round(legacy.last().grams)),
                           fixed(last, 1),
                           general(pct(last)));
    }
    return {};
}

std::string RecipeAudit::volume_summary() const
{
    if (customer_built)
    {
        return "\u2014";
    }
    if (volume.sim.frames.empty())
    {
        return "Nothing to dispense.";
    }
    const double last = volume.sim.last().ounces;
    switch (volume.status)
    {
    case VolumeFillStatus::FilledToLimit:
        return volume.passes_up == 0
                   ? std::format("Fits as written at {} oz; one more pass would not fit.", fixed(last, 1))
                   : std::format("Topped up {} pass{} to {} oz, {} g; the next pass would not "
                                 "fit.",
                                 volume.passes_up,
                                 volume.passes_up == 1 ? "" : "es",
                                 fixed(last, 1),
                                 general(std::round(volume.sim.last().grams)));
    case VolumeFillStatus::AtMax:
        return std::format(
            "Everything at its max at {} oz, {} oz under the limit.", fixed(last, 1), fixed(volume.limit_oz - last, 1));
    case VolumeFillStatus::SteppedDown:
        return std::format("Over as written at {} oz; {} pass{} down bring it to {} oz, proteins "
                           "untouched.",
                           fixed(volume.sim.first().ounces, 1),
                           volume.passes_down,
                           volume.passes_down == 1 ? "" : "es",
                           fixed(last, 1));
    case VolumeFillStatus::StillOver:
        return std::format("Still over at {} oz with bases and toppings stepped down as far as "
                           "allowed; the proteins or the recipe have to change.",
                           fixed(last, 1));
    }
    return {};
}

int RecipeAudit::severity() const
{
    if (customer_built)
    {
        return 0;
    }
    switch (legacy.verdict)
    {
    case Verdict::OverAtStart:
        return 5;
    case Verdict::OverWhileRamping:
        return 4;
    // A template that pins only a protein is meant to fall short: the customer adds
    // the rest. Only a finished bowl failing to fill is a defect.
    case Verdict::Saturated:
        return complete_bowl ? 3 : 0;
    case Verdict::NoFloor:
        return legacy.crossed_at_g ? 4 : 1;
    case Verdict::FitsMassBound:
        return 0;
    }
    return 0;
}

//
// ############################################################################
//

std::string BrandAudit::headline() const
{
    if (complete + partial == 0)
    {
        return brand + " defines no dispensable recipes.";
    }

    std::string head;
    if (legacy_failing() == 0)
    {
        head = complete == 1 ? std::format("{}: its one finished recipe comes out right as written. ", brand)
                             : std::format("{}: all {} finished recipes come out right as written. ", brand, complete);
    }
    else
    {
        std::vector<std::string> parts;
        if (legacy_over > 0)
        {
            parts.push_back(std::format("{} over the bowl", legacy_over));
        }
        if (legacy_short > 0)
        {
            parts.push_back(std::format("{} never reaching the weight floor", legacy_short));
        }
        head = std::format("{}: {} of {} finished recipes do not come out right as written — "
                           "{}. ",
                           brand,
                           legacy_failing(),
                           complete,
                           join(parts, " and "));
    }

    if (legacy_over > 0)
    {
        head += volume_over > 0 ? std::format("Volume fill leaves {} over even stepped down. ", volume_over)
                                : std::string("Volume fill brings every overfill back inside the bowl. ");
    }
    if (partial > 0)
    {
        head += std::format("{} template{} pin only a protein or two, so {} judged as a "
                            "modifier rather than a bowl.",
                            partial,
                            partial == 1 ? "" : "s",
                            partial == 1 ? "it is" : "they are");
    }
    return trim(head);
}

/// p_over_room,p_over_rim,p10_ml,median_ml,p90_ml, or n/a when there is no bowl.
static std::string fill_csv(const FillUncertainty &u)
{
    if (!u.valid)
    {
        return "n/a,n/a,n/a,n/a,n/a";
    }
    const double ml = CurveSet::kMlPerFlOz;
    return std::format("{},{},{},{},{}",
                       fixed(u.p_over_room, 3),
                       fixed(u.p_over_rim, 3),
                       fixed(u.p10_oz * ml, 0),
                       fixed(u.median_oz * ml, 0),
                       fixed(u.p90_oz * ml, 0));
}

std::string BrandAudit::to_csv() const
{
    std::string out = "brand,recipe,kind,pinned_ingredients,nominal_g,nominal_oz,floor_g,"
                      "legacy_verdict,legacy_final_oz,legacy_fill_pct,legacy_detail,"
                      // The bowl as a distribution: appended so existing columns keep their places.
                      "legacy_p_over_room,legacy_p_over_rim,legacy_p10_ml,legacy_median_ml,legacy_p90_ml,"
                      // Volume Fill, likewise appended.
                      "volume_status,volume_final_oz,volume_fill_pct,volume_passes,volume_detail,"
                      "volume_p_over_room,volume_p_over_rim,volume_p10_ml,volume_median_ml,volume_p90_ml\n";
    for (const RecipeAudit &r : rows)
    {
        const double cap = r.legacy.settings.bowl_capacity_oz;
        const double sauce = r.legacy.settings.overhead_oz();
        const double final_oz = r.legacy.frames.empty() ? 0.0 : r.legacy.last().ounces;
        out += std::format("{},{},{},{},{},{},{},{},{},{},{}",
                           csv_escape(brand),
                           csv_escape(r.recipe),
                           r.customer_built ? "customer-built" : (r.complete_bowl ? "complete" : "partial"),
                           r.pinned,
                           fixed(r.nominal_g, 0),
                           fixed(r.nominal_oz, 2),
                           r.floor_matched ? fixed(r.floor_g, 0) : std::string("none"),
                           r.customer_built ? std::string("n/a") : csv_escape(to_string(r.legacy.verdict)),
                           fixed(final_oz, 2),
                           r.customer_built || cap <= 0 ? std::string("n/a") : fixed((final_oz + sauce) / cap * 100, 0),
                           csv_escape(r.legacy_summary()));
        // The distribution columns follow.
        out += ',' + fill_csv(r.customer_built ? FillUncertainty{} : r.legacy.fill);
        // Volume Fill: status, where it ends, how many passes (+ up, - down), why, odds.
        const bool vf = !r.customer_built && !r.volume.sim.frames.empty();
        const double vf_oz = vf ? r.volume.sim.last().ounces : 0.0;
        const double vf_sauce = vf ? r.volume.sim.settings.overhead_oz() : 0.0;
        out += std::format(",{},{},{},{},{},",
                           vf ? csv_escape(to_string(r.volume.status)) : "n/a",
                           vf ? fixed(vf_oz, 2) : "n/a",
                           vf && cap > 0 ? fixed((vf_oz + vf_sauce) / cap * 100, 0) : "n/a",
                           vf ? std::to_string(r.volume.passes_up - r.volume.passes_down) : "n/a",
                           csv_escape(r.volume_summary())) +
               fill_csv(vf ? r.volume.sim.fill : FillUncertainty{}) + '\n';
    }
    return out;
}

BrandAudit audit_brand(const Menu &menu,
                       const CurveSet &curves,
                       const SimSettings &legacy_settings,
                       const VolumeFillSettings &volume_settings)
{
    BrandAudit audit;
    audit.brand = menu.brand;

    for (const Recipe &recipe : menu.recipes)
    {
        RecipeAudit row;
        row.recipe = recipe.name;
        row.customer_built = recipe.customer_built;
        row.pinned = static_cast<int>(recipe.items.size());
        row.complete_bowl = !recipe.customer_built && row.pinned >= kCompleteBowlThreshold;

        std::vector<BowlItem> items = bowl_from_recipe(menu, recipe, curves);

        std::vector<std::string> names;
        for (const BowlItem &it : items)
        {
            names.push_back(it.name);
        }
        if (const FloorRule *f = menu.floor_for(names))
        {
            row.floor_g = f->minimum_product_weight_g;
            row.floor_matched = true;
        }

        // A recipe that names its sauces is charged exactly those cups; one that names
        // none leaves the cups to the caller's setting, as a customer would choose.
        const int recipe_cups = std::min(static_cast<int>(menu.sauces_in(recipe).size()), SauceCups::kMax);

        SimSettings ls = legacy_settings;
        ls.floor_g = row.floor_g;
        if (recipe_cups > 0)
        {
            ls.sauce.cups = recipe_cups;
        }
        row.legacy = simulate(items, ls);
        if (!row.legacy.frames.empty())
        {
            row.nominal_oz = row.legacy.first().ounces;
            row.nominal_g = row.legacy.first().grams;
        }

        row.volume = volume_fill(items, ls, volume_settings);

        if (recipe.customer_built)
        {
            ++audit.customer_built;
        }
        else if (row.complete_bowl)
        {
            ++audit.complete;
        }
        else
        {
            ++audit.partial;
        }

        // Only a complete bowl is judged. A partial template is scored for overflow --
        // its pinned weights alone busting the bowl is a real finding -- but never for
        // falling short, which is what it is supposed to do.
        if (!recipe.customer_built)
        {
            if (row.legacy.verdict == Verdict::Saturated)
            {
                if (row.complete_bowl)
                {
                    ++audit.legacy_short;
                }
            }
            else if (!row.legacy_ok())
            {
                ++audit.legacy_over;
            }
            if (row.volume.status == VolumeFillStatus::StillOver)
            {
                ++audit.volume_over;
            }
            else if (row.volume.status == VolumeFillStatus::AtMax && row.complete_bowl)
            {
                ++audit.volume_short;
            }
        }
        audit.rows.push_back(std::move(row));
    }

    // Worst first, so the report opens on what needs fixing; below that, finished bowls
    // before the modifier templates and empty shells that nobody has to act on.
    auto rank = [](const RecipeAudit &r) { return r.customer_built ? 2 : r.complete_bowl ? 0 : 1; };
    std::stable_sort(audit.rows.begin(), audit.rows.end(), [&rank](const RecipeAudit &a, const RecipeAudit &b) {
        if (a.severity() != b.severity())
        {
            return a.severity() > b.severity();
        }
        if (rank(a) != rank(b))
        {
            return rank(a) < rank(b);
        }
        return a.nominal_oz > b.nominal_oz;
    });
    return audit;
}

} // namespace bowlfill
