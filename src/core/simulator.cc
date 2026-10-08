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

double SauceCups::volume_oz() const
{
    constexpr double kOzPerMl = 1.0 / 29.5735295625;
    return std::clamp(cups, 0, kMax) * cup_ml * kOzPerMl;
}

double food_capacity_oz(double bowl_capacity_oz, const SauceCups &sauce,
                        const BowlGeometry &g, double chunk_height_mm)
{
    if (!g.enabled || g.depth_mm <= 0)
        return std::max(0.0, bowl_capacity_oz - sauce.volume_oz());

    constexpr double kMlPerOz = 29.5735295625;
    const double cap_mm3 = bowl_capacity_oz * kMlPerOz * 1000.0;
    const double area = cap_mm3 / g.depth_mm;   // footprint of the equivalent cylinder
    const double dome = std::max(0.0, g.lid_headroom_mm);
    const double limit = g.depth_mm + dome;     // clearance at the centre of the lid

    // Volume below a level surface at height s. Above the rim the lid is a shallow
    // paraboloid, whose cross-section shrinks linearly to nothing at its crown.
    auto volume_below = [&](double s) {
        s = std::clamp(s, 0.0, limit);
        if (s <= g.depth_mm) return area * s;
        const double t = s - g.depth_mm;
        return area * (g.depth_mm + t - t * t / (2.0 * dome));
    };

    const int cups = std::clamp(sauce.cups, 0, SauceCups::kMax);
    const double chunk = std::max(0.0, chunk_height_mm) * std::clamp(g.chunk_proud, 0.0, 1.0);
    double food = 0.0;
    if (g.cups_pressed || cups == 0) {
        // The surface may rise to the lid less the chunks' proud height, except under
        // each cup, where it stops a cup height below the lid. The cup column is the
        // shortfall under the footprint.
        const double footprint = M_PI * std::pow(g.cup_diameter_mm / 2.0, 2);
        const double column = std::max(0.0, g.cup_height_mm - chunk);
        food = volume_below(limit - chunk) - cups * footprint * column;
    } else {
        // Cups on a level surface: everything stays a cup height below the lid.
        food = volume_below(limit - std::max(chunk, g.cup_height_mm));
    }
    return std::max(0.0, food) / 1000.0 / kMlPerOz;
}

double tallest_chunk_mm(const std::vector<BowlItem> &items)
{
    double tallest = 0.0;
    for (const BowlItem &it : items)
        if (it.start_g > 0 || it.final_g > 0) tallest = std::max(tallest, it.piece_height_mm);
    return tallest;
}

double SimSettings::food_capacity_oz() const
{
    return bowlfill::food_capacity_oz(bowl_capacity_oz, sauce, geometry, chunk_height_mm);
}

QString to_string(Verdict v)
{
    switch (v) {
    case Verdict::NoFloor: return "no floor";
    case Verdict::FitsMassBound: return "fits";
    case Verdict::Saturated: return "saturates short";
    case Verdict::OverAtStart: return "over as written";
    case Verdict::OverWhileRamping: return "over after ramping";
    }
    return {};
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
    result.settings.chunk_height_mm = tallest_chunk_mm(items);

    std::vector<double> weight(items.size());
    for (size_t i = 0; i < items.size(); ++i) weight[i] = items[i].start_g;

    // Loaded volume. The fit's own exponent already encodes self-compaction: a bed
    // of mass g carries a mean internal load of about g/2, and specific volume goes
    // as load^(p-1). Anchoring so that zero surcharge reproduces K*g^p exactly gives
    // A = K*2^(p-1), hence V = g * K * 2^(p-1) * (g/2 + phi*M)^(p-1). Only bases
    // compress; proteins and toppings are chunky and have no measurements.
    // A curve with p >= 1 shows no self-compaction, so there is nothing to scale with
    // load; it keeps its measured volume. (Clamping the exponent to 0 instead would
    // collapse it to K*g even with nothing on top -- most of the volume of a camera-
    // fitted curve with p > 1.)
    auto loaded_oz = [&](size_t i, double g) {
        const BowlItem &it = items[i];
        if (!settings.compress || !it.has_curve || g <= 0 || it.p >= 1.0)
            return it.volume_oz(g);
        double above = 0.0;
        for (size_t j = i + 1; j < items.size(); ++j) above += weight[j];
        const double e = it.p - 1.0;   // negative here: the bed compacts
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

    const double cap = result.settings.food_capacity_oz();
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

Fit composite_curve(Kind kind, const CurveSet &curves, Method method, QStringList *members)
{
    Fit out;
    if (kind == Kind::Base) return out;
    std::vector<const Fit *> fits;
    QStringList names;
    double lo = 0, hi = 0;
    for (const QString &n : curves.ingredients()) {
        if (n.startsWith("test", Qt::CaseInsensitive) || guess_kind(n) != kind) continue;
        const Fit *f = curves.fit(n, method);
        if (!f) continue;
        lo = fits.empty() ? f->lo_g : std::min(lo, f->lo_g);
        hi = fits.empty() ? f->hi_g : std::max(hi, f->hi_g);
        fits.push_back(f);
        names << n;
    }
    if (fits.empty() || hi <= lo) return out;

    // Average the members' predictions across the span they cover between them, then
    // fit one power law to that average, so the composite plugs in like any curve.
    constexpr int kSamples = 25;
    CurveSet avg;
    for (int i = 0; i < kSamples; ++i) {
        const double g = lo + (hi - lo) * i / (kSamples - 1);
        double oz = 0;
        for (const Fit *f : fits) oz += f->volume_oz(g);
        avg.add({"composite", method == Method::Pooled ? Method::Robot : method, g,
                 oz / fits.size()});
    }
    avg.refit();
    if (const Fit *f = avg.fit("composite", method)) {
        out = *f;
        out.n = static_cast<int>(fits.size());   // members, not samples
        out.lo_g = lo;
        out.hi_g = hi;
    }
    if (members) *members = names;
    return out;
}

CurveChoice choose_curve(const QString &name, Kind kind, const CurveSet &curves, Method method)
{
    CurveChoice c;
    if (const Fit *f = curves.fit(name, method)) {
        c.source = CurveSource::Own;
        c.K = f->K;
        c.p = f->p;
        return c;
    }
    if (const QString proxy = proxy_curve_for(name, curves); !proxy.isEmpty())
        if (const Fit *f = curves.fit(proxy, method)) {
            c.source = CurveSource::Family;
            c.K = f->K;
            c.p = f->p;
            c.from = {proxy};
            return c;
        }
    QStringList members;
    const Fit f = composite_curve(kind, curves, method, &members);
    if (f.valid) {
        c.source = CurveSource::Composite;
        c.K = f.K;
        c.p = f.p;
        c.from = members;
    }
    return c;
}

QString describe(const CurveChoice &choice)
{
    switch (choice.source) {
    case CurveSource::Own: return "own measurements";
    case CurveSource::Family: return QString("borrows %1").arg(choice.from.join(", "));
    case CurveSource::Composite:
        return QString("composite of %1").arg(choice.from.join(", "));
    case CurveSource::Flat: return "flat rate (nothing measured to lean on)";
    }
    return {};
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
    item.piece_height_mm = ingredient.piece_height_mm;

    const CurveChoice c = choose_curve(ingredient.name, ingredient.kind, curves, method);
    if (c.source != CurveSource::Flat) {
        item.has_curve = true;
        item.K = c.K;
        item.p = c.p;
    }
    return item;
}

}  // namespace bowlfill
