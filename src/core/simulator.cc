#include "simulator.hh"

#include <algorithm>
#include <cmath>

namespace bowlfill {

double BowlItem::volume_oz(double grams) const
{
    if (grams <= 0) return 0.0;
    return has_curve ? K * std::pow(grams, p) : grams / 100.0 * flat_oz_per_100g;
}

double BowlItem::marginal_oz_per_100g(double grams) const
{
    if (grams <= 0) return 0.0;
    return has_curve ? K * p * std::pow(grams, p - 1.0) * 100.0 : flat_oz_per_100g;
}

double SimResult::grams_per_pass() const
{
    double total = 0.0;
    for (const BowlItem &it : items)
        if (it.step_g > 0) total += it.step_g;
    return total;
}

double SimResult::base_share_of_step() const
{
    const double total = grams_per_pass();
    if (total <= 0) return 0.0;
    double bases = 0.0;
    for (const BowlItem &it : items)
        if (it.step_g > 0 && it.kind == Kind::Base) bases += it.step_g;
    return bases / total;
}

double SimResult::squeezed_oz() const
{
    return last().ounces_uncompressed - last().ounces;
}

//
// ############################################################################
//

SimResult simulate(std::vector<BowlItem> items, const SimSettings &settings)
{
    SimResult result;
    result.settings = settings;

    std::vector<double> weight(items.size());
    for (size_t i = 0; i < items.size(); ++i) weight[i] = items[i].start_g;

    // Loaded volume. The fit's own exponent already encodes self-compaction: a bed
    // of mass g carries a mean internal load of about g/2, and specific volume goes
    // as load^(p-1). Anchoring so that zero surcharge reproduces K*g^p exactly gives
    // A = K*2^(p-1), hence V = g * K * 2^(p-1) * (g/2 + phi*M)^(p-1). Only bases
    // compress; proteins and toppings are chunky and have no measurements.
    auto loaded_oz = [&](size_t i, double g) {
        const BowlItem &it = items[i];
        if (!settings.compress || !it.has_curve || g <= 0) return it.volume_oz(g);
        double above = 0.0;
        for (size_t j = i + 1; j < items.size(); ++j) above += weight[j];
        const double e = std::min(it.p - 1.0, 0.0);
        return g * it.K * std::pow(2.0, e)
               * std::pow(g / 2.0 + settings.load_transfer * above, e);
    };

    auto snapshot = [&] {
        Frame f;
        for (size_t i = 0; i < items.size(); ++i) {
            const double oz = loaded_oz(i, weight[i]);
            const double raw = items[i].volume_oz(weight[i]);
            f.per_item_oz.push_back(oz);
            f.per_item_oz_uncompressed.push_back(raw);
            f.grams += weight[i];
            f.ounces += oz;
            f.ounces_uncompressed += raw;
        }
        result.frames.push_back(std::move(f));
    };
    snapshot();

    int guard = 0;
    while (settings.floor_g > 0 && guard++ < 4000) {
        if (result.frames.back().grams > settings.floor_g) break;
        bool moved = false;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].step_g > 0 && weight[i] < items[i].max_g) {
                weight[i] = std::min(weight[i] + items[i].step_g, items[i].max_g);
                moved = true;
            }
        }
        if (!moved) break;
        snapshot();
    }

    for (size_t i = 0; i < items.size(); ++i) items[i].final_g = weight[i];
    result.items = std::move(items);
    result.passes = static_cast<int>(result.frames.size()) - 1;

    const double cap = settings.bowl_capacity_oz;
    result.saturated =
        settings.floor_g > 0 && result.frames.back().grams <= settings.floor_g;

    auto over = std::find_if(result.frames.begin(), result.frames.end(),
                             [cap](const Frame &f) { return f.ounces > cap; });
    const bool busts = over != result.frames.end();
    if (busts) result.crossed_at_g = over->grams;

    auto safe = std::find_if(result.frames.rbegin(), result.frames.rend(),
                             [cap](const Frame &f) { return f.ounces <= cap; });
    if (busts && safe != result.frames.rend()) result.highest_safe_floor_g = safe->grams;

    if (settings.floor_g <= 0)
        result.verdict = Verdict::NoFloor;
    else if (!busts)
        result.verdict = result.saturated ? Verdict::Saturated : Verdict::FitsMassBound;
    else if (over == result.frames.begin())
        result.verdict = Verdict::OverAtStart;
    else
        result.verdict = Verdict::OverWhileRamping;

    return result;
}

double split_base_start(const Ingredient &ingredient, bool split_on, bool two_bases)
{
    const double full = ingredient.full_portion();
    if (!split_on || !two_bases) return full;
    return std::max(std::round(full * 0.5), ingredient.per_portion_minimum_weight_g);
}

BowlItem make_item(const Ingredient &ingredient, const CurveSet &curves, Method method,
                   double flat_protein_rate, double flat_topping_rate)
{
    BowlItem item;
    item.name = ingredient.name;
    item.kind = ingredient.kind;
    item.start_g = ingredient.full_portion();
    item.step_g = ingredient.step_increment_g;
    item.max_g = ingredient.max_dispense_weight_g;
    item.flat_oz_per_100g =
        ingredient.kind == Kind::Protein ? flat_protein_rate : flat_topping_rate;

    QString curve_name = ingredient.name;
    if (const QString proxy = proxy_curve_for(ingredient.name, curves); !proxy.isEmpty())
        curve_name = proxy;
    if (const Fit *f = curves.fit(curve_name, method)) {
        item.has_curve = true;
        item.K = f->K;
        item.p = f->p;
    }
    return item;
}

}  // namespace bowlfill
