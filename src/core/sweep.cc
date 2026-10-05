#include "sweep.hh"

#include <QHash>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

qint64 choose(int n, int k)
{
    if (k < 0 || k > n) return 0;
    k = std::min(k, n - k);
    qint64 r = 1;
    for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return r;
}

/// Subsets of `n` items with between `lo` and `hi` members.
qint64 subsets(int n, int lo, int hi)
{
    qint64 total = 0;
    for (int k = lo; k <= std::min(hi, n); ++k) total += choose(n, k);
    return total;
}

///
/// Every subset of `pool` with between `lo` and `hi` members, as index lists. Held in
/// memory because the same combinations are replayed against every recipe, and the
/// largest pool here is 20 toppings capped at 4 -- some tens of thousands of small
/// vectors, not millions.
///
std::vector<std::vector<int>> enumerate(int n, int lo, int hi)
{
    std::vector<std::vector<int>> out;
    std::vector<int> pick;
    const int top = std::min(hi, n);
    std::function<void(int)> walk = [&](int start) {
        if (static_cast<int>(pick.size()) >= lo) out.push_back(pick);
        if (static_cast<int>(pick.size()) == top) return;
        for (int i = start; i < n; ++i) {
            pick.push_back(i);
            walk(i + 1);
            pick.pop_back();
        }
    };
    walk(0);
    return out;
}

QString csv_escape(const QString &s)
{
    return s.contains(',') || s.contains('"')
               ? "\"" + QString(s).replace("\"", "\"\"") + "\""
               : s;
}

/// Ingredient lists are joined rather than given a column each, so the row count
/// stays the only thing that grows.
QString join_names(const std::vector<const Ingredient *> &v)
{
    QString out;
    for (const Ingredient *i : v) {
        if (!out.isEmpty()) out += '|';
        out += i->name;
    }
    return csv_escape(out);
}

std::vector<const Ingredient *> pool_of(const Menu &menu, Kind kind)
{
    std::vector<const Ingredient *> out;
    for (const QString &n : menu.names_of(kind))
        if (const Ingredient *i = menu.find(n)) out.push_back(i);
    return out;
}

}  // namespace

qint64 combinations_for(const Menu &menu, const SweepLimits &limits)
{
    const int b = static_cast<int>(menu.names_of(Kind::Base).size());
    const int p = static_cast<int>(menu.names_of(Kind::Protein).size());
    const int t = static_cast<int>(menu.names_of(Kind::Topping).size());
    const int sauces = std::max(0, limits.max_sauces - limits.min_sauces + 1);
    return subsets(b, limits.min_bases, limits.max_bases)
           * subsets(p, limits.min_proteins, limits.max_proteins)
           * subsets(t, limits.min_toppings, limits.max_toppings) * sauces;
}

SweepPlan plan_sweep(const Menu &menu, const SweepLimits &limits)
{
    SweepPlan plan;
    plan.brand = menu.brand;
    plan.combinations = combinations_for(menu, limits);
    plan.recipes = static_cast<int>(menu.recipes.size());
    return plan;
}

QString sweep_csv_header()
{
    return "brand,recipe,recipe_kind,bases,proteins,toppings,sauce_cups,ingredients,"
           "nominal_g,nominal_oz,floor_g,legacy_verdict,legacy_final_g,legacy_final_oz,"
           "legacy_fill_pct,adaptive_status,adaptive_fill_pct,adaptive_alpha,cost\n";
}

bool run_sweep(const std::vector<const Menu *> &menus, const CurveSet &curves,
               Method method, const SimSettings &legacy_settings,
               const AdaptiveSettings &adaptive_settings, const CostTable *costs,
               const SweepLimits &limits, QTextStream &out,
               const std::function<bool(qint64, qint64)> &progress,
               std::vector<SweepTotals> *per_brand)
{
    qint64 total = 0;
    for (const Menu *m : menus) total += plan_sweep(*m, limits).rows();

    out << sweep_csv_header();

    qint64 done = 0;
    for (const Menu *menu : menus) {
        SweepTotals totals;
        const QString brand = csv_escape(menu->brand);

        const std::vector<const Ingredient *> bases = pool_of(*menu, Kind::Base);
        const std::vector<const Ingredient *> proteins = pool_of(*menu, Kind::Protein);
        const std::vector<const Ingredient *> toppings = pool_of(*menu, Kind::Topping);

        // One prototype per ingredient: the curve lookup and proxy matching are string
        // work, and repeating them per combination dominates everything else.
        QHash<QString, BowlItem> proto;
        for (const auto *pool : {&bases, &proteins, &toppings})
            for (const Ingredient *i : *pool)
                proto.insert(i->name, make_item(*i, curves, method));

        const auto base_sets = enumerate(static_cast<int>(bases.size()), limits.min_bases,
                                         limits.max_bases);
        const auto protein_sets = enumerate(static_cast<int>(proteins.size()),
                                            limits.min_proteins, limits.max_proteins);
        const auto topping_sets = enumerate(static_cast<int>(toppings.size()),
                                            limits.min_toppings, limits.max_toppings);

        // Recipe pins, resolved once per recipe rather than per row.
        struct RecipePins {
            QString name, kind;
            QHash<QString, double> grams;
        };
        std::vector<RecipePins> recipes;
        for (const Recipe &r : menu->recipes) {
            RecipePins rp;
            rp.name = csv_escape(r.name);
            rp.kind = r.customer_built ? "customer-built"
                      : static_cast<int>(r.items.size()) >= 4 ? "complete"
                                                              : "partial";
            for (const RecipeItem &ri : r.items) rp.grams.insert(ri.name, ri.grams);
            recipes.push_back(std::move(rp));
        }

        std::vector<const Ingredient *> combo;
        std::vector<BowlItem> items;
        QStringList names;
        QString line;

        for (const auto &bs : base_sets) {
            for (const auto &ps : protein_sets) {
                for (const auto &ts : topping_sets) {
                    combo.clear();
                    for (int i : bs) combo.push_back(bases[i]);
                    for (int i : ps) combo.push_back(proteins[i]);
                    for (int i : ts) combo.push_back(toppings[i]);

                    names.clear();
                    for (const Ingredient *i : combo) names << i->name;
                    const FloorRule *floor = menu->floor_for(names);
                    const double floor_g = floor ? floor->minimum_product_weight_g : 0.0;

                    const QString base_names = join_names(
                        {combo.begin(), combo.begin() + static_cast<int>(bs.size())});
                    const QString protein_names =
                        join_names({combo.begin() + static_cast<int>(bs.size()),
                                    combo.begin() + static_cast<int>(bs.size() + ps.size())});
                    const QString topping_names = join_names(
                        {combo.begin() + static_cast<int>(bs.size() + ps.size()), combo.end()});

                    const bool two_bases = bs.size() == 2;

                    for (int cups = limits.min_sauces; cups <= limits.max_sauces; ++cups)
                    for (const RecipePins &rp : recipes) {
                        items.clear();
                        for (size_t i = 0; i < combo.size(); ++i) {
                            const Ingredient *ing = combo[i];
                            BowlItem it = proto.value(ing->name);
                            // A weight the recipe pins is the spec; the 50:50 split only
                            // governs bases the recipe leaves to the customer.
                            auto pin = rp.grams.constFind(ing->name);
                            it.start_g = pin != rp.grams.constEnd()
                                             ? *pin
                                             : (i < bs.size()
                                                    ? split_base_start(*ing, true, two_bases)
                                                    : ing->full_portion());
                            items.push_back(it);
                        }

                        SimSettings ls = legacy_settings;
                        ls.floor_g = floor_g;
                        ls.compress = true;
                        ls.sauce.cups = cups;
                        AdaptiveSettings as = adaptive_settings;
                        as.sauce.cups = cups;
                        const SimResult sim = simulate(items, ls);
                        const AdaptiveResult ad = solve_adaptive(items, as, costs);

                        const double cap = ls.bowl_capacity_oz;
                        const double sauce_oz = sim.settings.overhead_oz();
                        const Frame &first = sim.first();
                        const Frame &last = sim.last();

                        line.clear();
                        QTextStream row(&line);
                        row << brand << ',' << rp.name << ',' << rp.kind << ','
                            << base_names << ',' << protein_names << ',' << topping_names
                            << ',' << cups << ',' << items.size() << ','
                            << QString::number(first.grams, 'f', 0) << ','
                            << QString::number(first.ounces, 'f', 2) << ','
                            << QString::number(floor_g, 'f', 0) << ','
                            << to_string(sim.verdict) << ','
                            << QString::number(last.grams, 'f', 0) << ','
                            << QString::number(last.ounces, 'f', 2) << ','
                            << QString::number(
                                   cap > 0 ? (last.ounces + sauce_oz) / cap * 100 : 0, 'f', 0)
                            << ',' << to_string(ad.status) << ','
                            << QString::number(ad.food_fill() * 100, 'f', 0) << ','
                            << QString::number(ad.alpha, 'f', 3) << ','
                            << QString::number(ad.total_cost, 'f', 2) << '\n';
                        out << line;

                        ++totals.rows;
                        if (sim.verdict == Verdict::Saturated) ++totals.legacy_short;
                        else if (sim.verdict == Verdict::FitsMassBound
                                 || (sim.verdict == Verdict::NoFloor && !sim.crossed_at_g))
                            ++totals.legacy_fits;
                        else ++totals.legacy_over;

                        if (ad.occupancy() > 1.0) ++totals.adaptive_over_bowl;
                        if (ad.status == AdaptiveStatus::StillOver) ++totals.adaptive_over;
                        else if (ad.status == AdaptiveStatus::Underfilled) ++totals.adaptive_short;
                        else ++totals.adaptive_ok;

                        if (++done % 4096 == 0 && progress && !progress(done, total))
                            return false;
                    }
                }
            }
        }
        if (per_brand) per_brand->push_back(totals);
    }
    if (progress) progress(done, total);
    return true;
}

}  // namespace bowlfill
