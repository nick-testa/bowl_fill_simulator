#include "feasibility.hh"

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

/// A template that pins only a protein is a modifier, not a bowl: the customer adds
/// the rest. Judging it as a finished bowl would report a dozen false failures.
constexpr int kCompleteBowlThreshold = 4;

QString csv_escape(const QString &s)
{
    return s.contains(',') || s.contains('"')
               ? "\"" + QString(s).replace("\"", "\"\"") + "\""
               : s;
}

}  // namespace

std::vector<BowlItem> bowl_from_recipe(const Menu &menu, const Recipe &recipe,
                                       const CurveSet &curves, Method method,
                                       double flat_protein_rate, double flat_topping_rate)
{
    std::vector<BowlItem> bases, proteins, toppings;
    for (const RecipeItem &ri : recipe.items) {
        const Ingredient *ing = menu.find(ri.name);
        if (!ing) continue;
        BowlItem item =
            make_item(*ing, curves, method, flat_protein_rate, flat_topping_rate);
        item.start_g = ri.grams;
        (ing->kind == Kind::Base      ? bases
         : ing->kind == Kind::Protein ? proteins
                                      : toppings)
            .push_back(item);
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
    switch (legacy.verdict) {
    case Verdict::FitsMassBound: return true;
    case Verdict::NoFloor: return !legacy.crossed_at_g.has_value();
    default: return false;
    }
}

QString RecipeAudit::legacy_summary() const
{
    if (customer_built)
        return "Customer builds this bowl; nothing is dispensed as written.";
    if (legacy.frames.empty()) return "No ingredients resolve from the menu.";

    // Percentages are of the whole bowl, so the cups belong in the numerator; the
    // verdicts themselves are judged against the space left for food.
    const double cap = legacy.settings.bowl_capacity_oz;
    const double sauce = legacy.settings.overhead_oz();
    const double last = legacy.last().ounces;
    const auto pct = [&](double oz) { return std::round((oz + sauce) / cap * 100); };
    switch (legacy.verdict) {
    case Verdict::OverAtStart: {
        const double start = legacy.first().ounces;
        return QString("Overflows as written — %1 oz at the specified weights, %2% of "
                       "the bowl, and the ramp takes it to %3 oz.")
            .arg(start, 0, 'f', 1)
            .arg(pct(start))
            .arg(last, 0, 'f', 1);
    }
    case Verdict::OverWhileRamping:
        return QString("Fits as written, then the ramp pushes it over: %1 oz at the "
                       "floor, crossing the limit at %2 g.")
            .arg(last, 0, 'f', 1)
            .arg(std::round(*legacy.crossed_at_g));
    case Verdict::Saturated:
        return QString("Never reaches its %1 g floor — everything saturates at %2 g, "
                       "%3 oz.")
            .arg(std::round(legacy.settings.floor_g))
            .arg(std::round(legacy.last().grams))
            .arg(last, 0, 'f', 1);
    case Verdict::NoFloor:
        return legacy.crossed_at_g
                   ? QString("No floor matches this bowl, and it is over anyway at "
                             "%1 oz.").arg(last, 0, 'f', 1)
                   : QString("No floor matches this bowl, so the ramp never fires. "
                             "Dispensed as written at %1 oz.").arg(last, 0, 'f', 1);
    case Verdict::FitsMassBound:
        return QString("Fits: clears the floor at %1 g and %2 oz, %3% of the bowl.")
            .arg(std::round(legacy.last().grams))
            .arg(last, 0, 'f', 1)
            .arg(pct(last));
    }
    return {};
}

QString RecipeAudit::adaptive_summary() const
{
    if (customer_built) return "—";
    switch (adaptive.status) {
    case AdaptiveStatus::NoAdjustment:
        return QString("Already inside the band at %1% — dispensed unchanged.")
            .arg(std::round(adaptive.food_fill() * 100));
    case AdaptiveStatus::Adjusted:
        return QString("Solved to %1% by moving every ingredient %2% of its band.")
            .arg(std::round(adaptive.food_fill() * 100))
            .arg(std::fabs(adaptive.alpha) * 100, 0, 'f', 0);
    case AdaptiveStatus::StillOver:
        return QString("Still over at full reduction: %1% of the bowl. Tolerances "
                       "cannot rescue it; the spec has to change.")
            .arg(std::round(adaptive.food_fill() * 100));
    case AdaptiveStatus::Underfilled:
        return QString("Cannot reach the target even at full increase — %1%.")
            .arg(std::round(adaptive.food_fill() * 100));
    case AdaptiveStatus::NoIngredients:
        return "Nothing to dispense.";
    }
    return {};
}

int RecipeAudit::severity() const
{
    if (customer_built) return 0;
    switch (legacy.verdict) {
    case Verdict::OverAtStart: return 5;
    case Verdict::OverWhileRamping: return 4;
    // A template that pins only a protein is meant to fall short: the customer adds
    // the rest. Only a finished bowl failing to fill is a defect.
    case Verdict::Saturated: return complete_bowl ? 3 : 0;
    case Verdict::NoFloor: return legacy.crossed_at_g ? 4 : 1;
    case Verdict::FitsMassBound: return 0;
    }
    return 0;
}

//
// ############################################################################
//

QString BrandAudit::headline() const
{
    if (complete + partial == 0)
        return QString("%1 defines no dispensable recipes.").arg(brand);

    QString head;
    if (legacy_failing() == 0) {
        head = complete == 1
                   ? QString("%1: its one finished recipe comes out right as written. ")
                         .arg(brand)
                   : QString("%1: all %2 finished recipes come out right as written. ")
                         .arg(brand)
                         .arg(complete);
    } else {
        QStringList parts;
        if (legacy_over > 0) parts << QString("%1 over the bowl").arg(legacy_over);
        if (legacy_short > 0)
            parts << QString("%1 never reaching the weight floor").arg(legacy_short);
        head = QString("%1: %2 of %3 finished recipes do not come out right as written — "
                       "%4. ")
                   .arg(brand)
                   .arg(legacy_failing())
                   .arg(complete)
                   .arg(parts.join(" and "));
    }

    if (legacy_over > 0)
        head += adaptive_over > 0
                    ? QString("Adaptive rescues all but %1 of the overfills. ").arg(adaptive_over)
                    : QString("Adaptive brings every overfill back inside the bowl. ");
    if (adaptive_short > 0)
        head += QString("%1 cannot reach the fill target on the ingredients specified. ")
                    .arg(adaptive_short);
    if (partial > 0)
        head += QString("%1 template%2 pin only a protein or two, so %3 judged as a "
                        "modifier rather than a bowl.")
                    .arg(partial)
                    .arg(partial == 1 ? "" : "s")
                    .arg(partial == 1 ? "it is" : "they are");
    return head.trimmed();
}

QString BrandAudit::to_csv() const
{
    QString out =
        "brand,recipe,kind,pinned_ingredients,nominal_g,nominal_oz,floor_g,"
        "legacy_verdict,legacy_final_oz,legacy_fill_pct,legacy_detail,"
        "adaptive_status,adaptive_fill_pct,adaptive_detail\n";
    for (const RecipeAudit &r : rows) {
        const double cap = r.legacy.settings.bowl_capacity_oz;
        const double sauce = r.legacy.settings.overhead_oz();
        const double final_oz = r.legacy.frames.empty() ? 0.0 : r.legacy.last().ounces;
        out += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14\n")
                   .arg(csv_escape(brand), csv_escape(r.recipe),
                        r.customer_built ? "customer-built"
                                         : (r.complete_bowl ? "complete" : "partial"))
                   .arg(r.pinned)
                   .arg(r.nominal_g, 0, 'f', 0)
                   .arg(r.nominal_oz, 0, 'f', 2)
                   .arg(r.floor_matched ? QString::number(r.floor_g, 'f', 0)
                                        : QString("none"))
                   .arg(r.customer_built ? QString("n/a")
                                         : csv_escape(to_string(r.legacy.verdict)))
                   .arg(final_oz, 0, 'f', 2)
                   .arg(r.customer_built || cap <= 0
                            ? QString("n/a")
                            : QString::number((final_oz + sauce) / cap * 100, 'f', 0))
                   .arg(csv_escape(r.legacy_summary()),
                        r.customer_built ? QString("n/a")
                                         : csv_escape(to_string(r.adaptive.status)))
                   .arg(r.customer_built ? 0.0 : r.adaptive.food_fill() * 100, 0, 'f', 0)
                   .arg(csv_escape(r.adaptive_summary()));
    }
    return out;
}

BrandAudit audit_brand(const Menu &menu, const CurveSet &curves, Method method,
                       const SimSettings &legacy_settings,
                       const AdaptiveSettings &adaptive_settings, const CostTable *costs)
{
    BrandAudit audit;
    audit.brand = menu.brand;

    for (const Recipe &recipe : menu.recipes) {
        RecipeAudit row;
        row.recipe = recipe.name;
        row.customer_built = recipe.customer_built;
        row.pinned = static_cast<int>(recipe.items.size());
        row.complete_bowl = !recipe.customer_built && row.pinned >= kCompleteBowlThreshold;

        std::vector<BowlItem> items = bowl_from_recipe(menu, recipe, curves, method);

        QStringList names;
        for (const BowlItem &it : items) names << it.name;
        if (const FloorRule *f = menu.floor_for(names)) {
            row.floor_g = f->minimum_product_weight_g;
            row.floor_matched = true;
        }

        // A recipe that names its sauces is charged exactly those cups; one that names
        // none leaves the cups to the caller's setting, as a customer would choose.
        const int recipe_cups =
            std::min(static_cast<int>(menu.sauces_in(recipe).size()), SauceCups::kMax);

        SimSettings ls = legacy_settings;
        ls.floor_g = row.floor_g;
        if (recipe_cups > 0) ls.sauce.cups = recipe_cups;
        row.legacy = simulate(items, ls);
        if (!row.legacy.frames.empty()) {
            row.nominal_oz = row.legacy.first().ounces;
            row.nominal_g = row.legacy.first().grams;
        }

        AdaptiveSettings as = adaptive_settings;
        if (recipe_cups > 0) as.sauce.cups = recipe_cups;
        row.adaptive = solve_adaptive(items, as, costs);

        if (recipe.customer_built) ++audit.customer_built;
        else if (row.complete_bowl) ++audit.complete;
        else ++audit.partial;

        // Only a complete bowl is judged. A partial template is scored for overflow --
        // its pinned weights alone busting the bowl is a real finding -- but never for
        // falling short, which is what it is supposed to do.
        if (!recipe.customer_built) {
            if (row.legacy.verdict == Verdict::Saturated) {
                if (row.complete_bowl) ++audit.legacy_short;
            } else if (!row.legacy_ok()) {
                ++audit.legacy_over;
            }
            // "Still over" in the solver means it could not reach the target; only a
            // bowl past its capacity is an overfill the report should claim.
            if (row.adaptive.occupancy() > 1.0) ++audit.adaptive_over;
            if (row.adaptive.status == AdaptiveStatus::Underfilled && row.complete_bowl)
                ++audit.adaptive_short;
        }
        audit.rows.push_back(std::move(row));
    }

    // Worst first, so the report opens on what needs fixing; below that, finished bowls
    // before the modifier templates and empty shells that nobody has to act on.
    auto rank = [](const RecipeAudit &r) {
        return r.customer_built ? 2 : r.complete_bowl ? 0 : 1;
    };
    std::stable_sort(audit.rows.begin(), audit.rows.end(),
                     [&rank](const RecipeAudit &a, const RecipeAudit &b) {
                         if (a.severity() != b.severity()) return a.severity() > b.severity();
                         if (rank(a) != rank(b)) return rank(a) < rank(b);
                         return a.nominal_oz > b.nominal_oz;
                     });
    return audit;
}

}  // namespace bowlfill
