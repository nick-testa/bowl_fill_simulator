#include "simulator.hh"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bowlfill
{

double BowlItem::volume_oz(double grams) const
{
    if (grams <= 0)
    {
        return 0.0;
    }
    return has_curve ? K * std::pow(grams, p) : grams / 100.0 * flat_oz_per_100g;
}

double BowlItem::marginal_oz_per_100g(double grams) const
{
    if (grams <= 0)
    {
        return 0.0;
    }
    return has_curve ? K * p * std::pow(grams, p - 1.0) * 100.0 : flat_oz_per_100g;
}

double SauceCups::volume_oz() const
{
    constexpr double kOzPerMl = 1.0 / 29.5735295625;
    return std::clamp(cups, 0, kMax) * cup_ml * kOzPerMl;
}

namespace
{
constexpr double kMlPerOz = 29.5735295625;
}

double BowlGeometry::volume_below_mm3(double height_mm) const
{
    const double h = std::clamp(height_mm, 0.0, lid_mm());
    const double r0 = bottom_diameter_mm / 2.0, rt = top_diameter_mm / 2.0;
    auto frustum = [&](double a) { // the bowl's bottom `a` mm
        const double r1 = depth_mm > 0 ? r0 + (rt - r0) * a / depth_mm : r0;
        return M_PI * a / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1);
    };
    if (h <= depth_mm)
    {
        return frustum(h);
    }
    // Over the rim, only the lid's raised centre has room.
    const double plateau = M_PI * std::pow(std::min(lid_plateau_diameter_mm, top_diameter_mm) / 2.0, 2);
    return frustum(depth_mm) + plateau * (h - depth_mm);
}

double BowlGeometry::height_of_mm3(double volume_mm3) const
{
    double lo = 0.0, hi = lid_mm();
    if (volume_mm3 >= volume_below_mm3(hi))
    {
        return hi;
    }
    for (int i = 0; i < 60; ++i)
    {
        const double mid = (lo + hi) / 2.0;
        (volume_below_mm3(mid) < volume_mm3 ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

namespace
{

/// Food, in mm³, that brings the surface to `surface_mm` with pressed-in cups whose
/// tops sit at the lid: each displaces whatever of it is below the surface.
double food_to_surface_mm3(const BowlGeometry &g, int cups, double surface_mm)
{
    const double cup_floor = g.lid_mm() - g.cup_height_mm;
    return g.volume_below_mm3(surface_mm) - cups * g.cup_volume_below_mm3(surface_mm - cup_floor);
}

} // namespace

double food_capacity_oz(double bowl_capacity_oz, const SauceCups &sauce, const BowlGeometry &g, double chunk_height_mm)
{
    if (!g.enabled || g.depth_mm <= 0)
    {
        return std::max(0.0, bowl_capacity_oz - sauce.volume_oz());
    }

    const int cups = std::clamp(sauce.cups, 0, SauceCups::kMax);
    const double chunk = std::max(0.0, chunk_height_mm) * std::clamp(g.chunk_proud, 0.0, 1.0);
    const double fill_line = g.height_of_mm3(bowl_capacity_oz * kMlPerOz * 1000.0);
    // The surface may rise to the fill line, unless what stands on it would then
    // touch the lid: a chunk's proud part always, and resting cups their whole height.
    double clear = chunk;
    if (!g.cups_pressed && cups > 0)
    {
        clear = std::max(clear, g.cup_height_mm);
    }
    const double surface = std::min(fill_line, g.lid_mm() - clear);
    const double food = g.cups_pressed ? food_to_surface_mm3(g, cups, surface) : g.volume_below_mm3(surface);
    return std::max(0.0, food) / 1000.0 / kMlPerOz;
}

double rim_food_oz(const SauceCups &sauce, const BowlGeometry &g)
{
    const double rim = g.rim_ml() / kMlPerOz;
    if (!g.enabled)
    {
        return std::max(0.0, rim - sauce.volume_oz());
    }
    const int cups = std::clamp(sauce.cups, 0, SauceCups::kMax);
    // Resting cups ride on the food and take nothing below the rim.
    const double food = g.cups_pressed ? food_to_surface_mm3(g, cups, g.depth_mm) : g.volume_below_mm3(g.depth_mm);
    return std::max(0.0, food) / 1000.0 / kMlPerOz;
}

double food_surface_mm(const SauceCups &sauce, const BowlGeometry &g, double food_oz)
{
    const double v = std::max(0.0, food_oz) * kMlPerOz * 1000.0;
    const int cups = g.enabled && g.cups_pressed ? std::clamp(sauce.cups, 0, SauceCups::kMax) : 0;
    const double lid = g.lid_mm();
    const double at_lid = food_to_surface_mm3(g, cups, lid);
    if (v >= at_lid)
    {
        const double rim_area = M_PI * std::pow(g.top_diameter_mm / 2.0, 2);
        return rim_area > 0 ? lid + (v - at_lid) / rim_area : lid;
    }
    double lo = 0.0, hi = lid;
    for (int i = 0; i < 60; ++i)
    {
        const double mid = (lo + hi) / 2.0;
        (food_to_surface_mm3(g, cups, mid) < v ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

namespace
{
constexpr int kMoundRings = 300;
}

std::vector<double> mound_radii(const BowlGeometry &g)
{
    std::vector<double> r(kMoundRings);
    const double dr = g.top_diameter_mm / 2.0 / kMoundRings;
    for (int i = 0; i < kMoundRings; ++i)
    {
        r[i] = (i + 0.5) * dr;
    }
    return r;
}

std::vector<std::vector<double>> mound_surfaces(const BowlGeometry &g, const std::vector<MoundLayer> &layers)
{
    const std::vector<double> r = mound_radii(g);
    const double r0 = g.bottom_diameter_mm / 2.0, rt = g.top_diameter_mm / 2.0;
    const double dr = rt / kMoundRings;
    // The empty bowl: flat floor, then the wall rising to the rim.
    std::vector<double> floor(r.size());
    for (size_t i = 0; i < r.size(); ++i)
    {
        floor[i] = r[i] <= r0 || rt <= r0 ? 0.0 : g.depth_mm * (r[i] - r0) / (rt - r0);
    }
    std::vector<std::vector<double>> out{floor};

    for (const MoundLayer &layer : layers)
    {
        const std::vector<double> &below = out.back();
        const double slope = std::tan(std::clamp(layer.slope_deg, 0.0, 80.0) * M_PI / 180.0);
        // Where it comes to rest: the centre, unless it rolls. A rolling layer runs
        // outward while the ground falls away more steeply than its roll-off angle.
        double rest = 0.0;
        if (layer.roll_off_deg >= 0)
        {
            const double roll = std::tan(std::clamp(layer.roll_off_deg, 0.0, 80.0) * M_PI / 180.0);
            size_t i = 0;
            while (i + 1 < r.size() && (below[i] - below[i + 1]) / dr > roll + 1e-9)
            {
                ++i;
            }
            rest = r[i];
        }
        // Its pile: a cone from where it rests, a ridge round the bowl when that is
        // off-centre.
        auto pile = [&](double h, size_t i) { return h - slope * std::fabs(r[i] - rest); };
        // Volume this layer adds with its pile's top at h: each ring of the bowl rises
        // to the pile where the pile is higher than what is already there.
        auto added = [&](double h) {
            double v = 0.0;
            for (size_t i = 0; i < r.size(); ++i)
            {
                v += 2.0 * M_PI * r[i] * dr * std::max(0.0, pile(h, i) - below[i]);
            }
            return v;
        };
        double lo = 0.0, hi = std::max(1.0, below.front() + g.depth_mm);
        while (added(hi) < layer.volume_mm3 && hi < 1e5)
        {
            hi *= 2;
        }
        for (int i = 0; i < 60; ++i)
        {
            const double mid = (lo + hi) / 2.0;
            (added(mid) < layer.volume_mm3 ? lo : hi) = mid;
        }
        const double h = (lo + hi) / 2.0;
        std::vector<double> next(r.size());
        for (size_t i = 0; i < r.size(); ++i)
        {
            next[i] = std::max(below[i], pile(h, i));
        }
        out.push_back(std::move(next));
    }
    return out;
}

std::vector<MoundLayer> mound_layers(const std::vector<BowlItem> &items,
                                     const std::vector<double> &item_oz,
                                     const BowlGeometry &g,
                                     double scale)
{
    std::vector<MoundLayer> out;
    for (size_t i = 0; i < items.size() && i < item_oz.size(); ++i)
    {
        if (item_oz[i] <= 0.005)
        {
            continue;
        }
        const double slope = items[i].mound_slope_deg >= 0 ? items[i].mound_slope_deg : g.mound_slope_deg;
        out.push_back({item_oz[i] * scale * kMlPerOz * 1000.0, slope, items[i].roll_off_deg});
    }
    return out;
}

double mound_peak_mm(const BowlGeometry &g, const std::vector<MoundLayer> &layers)
{
    return layers.empty() ? 0.0 : highest_food_mm(mound_surfaces(g, layers));
}

double highest_food_mm(const std::vector<std::vector<double>> &surfaces)
{
    if (surfaces.empty())
    {
        return 0.0;
    }
    // Where there is food: the top stands above the empty bowl (the first surface),
    // whose walls are not food however high they reach. A ridge of rolled chunks can
    // stand above the centre, so this is the highest such ring, not the middle.
    const std::vector<double> &floor = surfaces.front(), &top = surfaces.back();
    double high = 0.0;
    for (size_t i = 0; i < top.size() && i < floor.size(); ++i)
    {
        if (top[i] > floor[i] + 1e-9)
        {
            high = std::max(high, top[i]);
        }
    }
    return high;
}

double BowlGeometry::cup_volume_below_mm3(double height_mm) const
{
    if (cup_height_mm <= 0)
    {
        return 0.0;
    }
    const double a = std::clamp(height_mm, 0.0, cup_height_mm);
    const double r0 = cup_base_diameter_mm / 2.0;
    const double r1 = r0 + (cup_diameter_mm - cup_base_diameter_mm) / 2.0 * a / cup_height_mm;
    return M_PI * a / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1); // frustum of height a
}

double tallest_chunk_mm(const std::vector<BowlItem> &items)
{
    double tallest = 0.0;
    for (const BowlItem &it : items)
    {
        if (it.start_g > 0 || it.final_g > 0)
        {
            tallest = std::max(tallest, it.piece_height_mm);
        }
    }
    return tallest;
}

double SimSettings::food_capacity_oz() const
{
    return bowlfill::food_capacity_oz(bowl_capacity_oz, sauce, geometry, chunk_height_mm);
}

std::string to_string(Verdict v)
{
    switch (v)
    {
    case Verdict::NoFloor:
        return "no floor";
    case Verdict::FitsMassBound:
        return "fits";
    case Verdict::Saturated:
        return "saturates short";
    case Verdict::OverAtStart:
        return "over as written";
    case Verdict::OverWhileRamping:
        return "over after ramping";
    }
    return {};
}

double SimResult::grams_per_pass() const
{
    double total = 0.0;
    for (const BowlItem &it : items)
    {
        if (it.step_g > 0)
        {
            total += it.step_g;
        }
    }
    return total;
}

double SimResult::base_share_of_step() const
{
    const double total = grams_per_pass();
    if (total <= 0)
    {
        return 0.0;
    }
    double bases = 0.0;
    for (const BowlItem &it : items)
    {
        if (it.step_g > 0 && it.kind == Kind::Base)
        {
            bases += it.step_g;
        }
    }
    return bases / total;
}

double SimResult::squeezed_oz() const
{
    return last().ounces_uncompressed - last().ounces;
}

//
// ############################################################################
//

Frame bowl_frame(const std::vector<BowlItem> &items, const std::vector<double> &grams, const SimSettings &settings)
{
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
        {
            return it.volume_oz(g);
        }
        double above = 0.0;
        for (size_t j = i + 1; j < items.size(); ++j)
        {
            above += grams[j];
        }
        const double e = it.p - 1.0; // negative here: the bed compacts
        return g * it.K * std::pow(2.0, e) * std::pow(g / 2.0 + settings.load_transfer * above, e);
    };
    Frame f;
    for (size_t i = 0; i < items.size() && i < grams.size(); ++i)
    {
        const double oz = loaded_oz(i, grams[i]);
        const double raw = items[i].volume_oz(grams[i]);
        f.per_item_oz.push_back(oz);
        f.per_item_oz_uncompressed.push_back(raw);
        f.grams += grams[i];
        f.ounces += oz;
        f.ounces_uncompressed += raw;
    }
    return f;
}

SimResult simulate(std::vector<BowlItem> items, const SimSettings &settings)
{
    SimResult result;
    result.settings = settings;
    result.settings.chunk_height_mm = tallest_chunk_mm(items);

    std::vector<double> weight(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        weight[i] = items[i].start_g;
    }

    auto snapshot = [&] { result.frames.push_back(bowl_frame(items, weight, settings)); };
    snapshot();

    int guard = 0;
    while (settings.floor_g > 0 && guard++ < 4000)
    {
        if (result.frames.back().grams > settings.floor_g)
        {
            break;
        }
        bool moved = false;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].step_g > 0 && weight[i] < items[i].max_g)
            {
                weight[i] = std::min(weight[i] + items[i].step_g, items[i].max_g);
                moved = true;
            }
        }
        if (!moved)
        {
            break;
        }
        snapshot();
    }

    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i].final_g = weight[i];
    }
    result.items = std::move(items);
    result.passes = static_cast<int>(result.frames.size()) - 1;

    const double cap = result.settings.food_capacity_oz();
    result.saturated = settings.floor_g > 0 && result.frames.back().grams <= settings.floor_g;

    auto over =
        std::find_if(result.frames.begin(), result.frames.end(), [cap](const Frame &f) { return f.ounces > cap; });
    const bool busts = over != result.frames.end();
    if (busts)
    {
        result.crossed_at_g = over->grams;
    }

    auto safe =
        std::find_if(result.frames.rbegin(), result.frames.rend(), [cap](const Frame &f) { return f.ounces <= cap; });
    if (busts && safe != result.frames.rend())
    {
        result.highest_safe_floor_g = safe->grams;
    }

    if (settings.floor_g <= 0)
    {
        result.verdict = Verdict::NoFloor;
    }
    else if (!busts)
    {
        result.verdict = result.saturated ? Verdict::Saturated : Verdict::FitsMassBound;
    }
    else if (over == result.frames.begin())
    {
        result.verdict = Verdict::OverAtStart;
    }
    else
    {
        result.verdict = Verdict::OverWhileRamping;
    }

    result.fill = fill_uncertainty(
        result.items, result.last().per_item_oz, cap, result.settings.rim_oz(), &result.settings.dispense);
    return result;
}

std::string to_string(VolumeFillStatus s)
{
    switch (s)
    {
    case VolumeFillStatus::FilledToLimit:
        return "filled to the limit";
    case VolumeFillStatus::AtMax:
        return "at max under the limit";
    case VolumeFillStatus::SteppedDown:
        return "stepped down to fit";
    case VolumeFillStatus::StillOver:
        return "still over at the lowest";
    }
    return {};
}

VolumeFillResult volume_fill(std::vector<BowlItem> items, const SimSettings &settings, const VolumeFillSettings &vf)
{
    VolumeFillResult out;
    SimResult &result = out.sim;
    result.settings = settings;
    result.settings.floor_g = 0; // volume alone decides; no floor is reported against
    result.settings.chunk_height_mm = tallest_chunk_mm(items);
    const double room = result.settings.food_capacity_oz();
    out.limit_oz = std::max(0.0, vf.fit_share) * room;

    std::vector<double> weight(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        weight[i] = items[i].start_g;
    }
    result.frames.push_back(bowl_frame(items, weight, settings));
    auto fits = [&](const Frame &f) { return f.ounces <= out.limit_oz + 1e-9; };

    int guard = 0;
    if (fits(result.frames.back()))
    {
        // Up to the limit, a whole pass at a time, never into overfill. Weight and
        // the floor do not enter into it.
        out.status = VolumeFillStatus::FilledToLimit;
        while (guard++ < 4000)
        {
            std::vector<double> next = weight;
            bool moved = false;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (items[i].step_g > 0 && next[i] < items[i].max_g)
                {
                    next[i] = std::min(next[i] + items[i].step_g, items[i].max_g);
                    moved = true;
                }
            }
            if (!moved)
            {
                out.status = VolumeFillStatus::AtMax;
                break;
            }
            Frame f = bowl_frame(items, next, settings);
            if (!fits(f))
            {
                break;
            }
            weight = std::move(next);
            result.frames.push_back(std::move(f));
            ++out.passes_up;
        }
    }
    else
    {
        // Over as written: bases and toppings come down a step per pass; proteins
        // keep their portion. Nothing goes below its share of the recipe portion.
        out.status = VolumeFillStatus::SteppedDown;
        while (!fits(result.frames.back()) && guard++ < 4000)
        {
            bool moved = false;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const BowlItem &it = items[i];
                if (it.kind == Kind::Protein || it.step_g <= 0)
                {
                    continue;
                }
                const double lowest = it.start_g * std::clamp(vf.min_share, 0.0, 1.0);
                if (weight[i] > lowest + 1e-9)
                {
                    weight[i] = std::max(weight[i] - it.step_g, lowest);
                    moved = true;
                }
            }
            if (!moved)
            {
                out.status = VolumeFillStatus::StillOver;
                break;
            }
            result.frames.push_back(bowl_frame(items, weight, settings));
            ++out.passes_down;
        }
    }

    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i].final_g = weight[i];
    }
    result.items = std::move(items);
    result.passes = static_cast<int>(result.frames.size()) - 1;
    result.saturated = out.status == VolumeFillStatus::AtMax;
    result.verdict = out.status == VolumeFillStatus::StillOver ? Verdict::OverAtStart
                     : out.status == VolumeFillStatus::AtMax   ? Verdict::Saturated
                                                               : Verdict::FitsMassBound;
    result.fill = fill_uncertainty(
        result.items, result.last().per_item_oz, room, result.settings.rim_oz(), &result.settings.dispense);
    return out;
}

double split_base_start(const Ingredient &ingredient, bool split_on, bool two_bases)
{
    const double full = ingredient.full_portion();
    if (!split_on || !two_bases)
    {
        return full;
    }
    return std::max(std::round(full * 0.5), ingredient.per_portion_minimum_weight_g);
}

double DispenseVariation::for_kind(Kind kind) const
{
    switch (kind)
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

double DispenseVariation::bias_for(Kind kind) const
{
    switch (kind)
    {
    case Kind::Base:
        return base_bias;
    case Kind::Protein:
        return protein_bias;
    case Kind::Topping:
        return topping_bias;
    }
    return topping_bias;
}

FillUncertainty fill_uncertainty(const std::vector<BowlItem> &items,
                                 const std::vector<double> &item_oz,
                                 double room_oz,
                                 double rim_oz,
                                 const DispenseVariation *dispense)
{
    FillUncertainty u;
    if (items.empty() || items.size() != item_oz.size())
    {
        return u;
    }
    double var = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const BowlItem &it = items[i];
        double rel2 = it.rel_sd * it.rel_sd + it.curve_rel_se * it.curve_rel_se;
        const double p = it.has_curve ? it.p : 1.0;
        double oz = item_oz[i];
        // A share d off in mass moves volume by p * d along a power law, and a robot
        // that runs heavy by b on average moves the whole item by (1 + b)^p.
        if (dispense)
        {
            const double d = p * dispense->for_kind(it.kind);
            rel2 += d * d;
            oz *= std::pow(1.0 + dispense->bias_for(it.kind), p);
        }
        u.median_oz += oz;
        var += oz * oz * rel2;
    }
    u.sd_oz = std::sqrt(var);
    u.p10_oz = u.median_oz - kZ80 * u.sd_oz;
    u.p90_oz = u.median_oz + kZ80 * u.sd_oz;
    u.room_oz = room_oz;
    u.rim_oz = rim_oz;
    auto p_over = [&](double limit) {
        if (u.sd_oz <= 0)
        {
            return u.median_oz > limit ? 1.0 : 0.0;
        }
        return 0.5 * std::erfc((limit - u.median_oz) / (u.sd_oz * std::sqrt(2.0)));
    };
    u.p_over_room = p_over(room_oz);
    u.p_over_rim = p_over(rim_oz);
    u.valid = true;
    return u;
}

Fit composite_curve(Kind kind, const CurveSet &curves, Method method, std::vector<std::string> *members)
{
    Fit out;
    if (kind == Kind::Base)
    {
        return out;
    }
    std::vector<const Fit *> fits;
    std::vector<std::string> names;
    double lo = 0, hi = 0;
    for (const std::string &n : curves.ingredients())
    {
        if (istarts_with(n, "test") || guess_kind(n) != kind)
        {
            continue;
        }
        const Fit *f = curves.fit(n, method);
        if (!f)
        {
            continue;
        }
        lo = fits.empty() ? f->lo_g : std::min(lo, f->lo_g);
        hi = fits.empty() ? f->hi_g : std::max(hi, f->hi_g);
        fits.push_back(f);
        names.push_back(n);
    }
    if (fits.empty() || hi <= lo)
    {
        return out;
    }

    // Average the members' predictions across the span they cover between them, then
    // fit one power law to that average, so the composite plugs in like any curve.
    constexpr int kSamples = 25;
    CurveSet avg;
    for (int i = 0; i < kSamples; ++i)
    {
        const double g = lo + (hi - lo) * i / (kSamples - 1);
        double oz = 0;
        for (const Fit *f : fits)
        {
            oz += f->volume_oz(g);
        }
        avg.add({"composite", method == Method::Pooled ? Method::Robot : method, g, oz / fits.size()});
    }
    avg.refit();
    if (const Fit *f = avg.fit("composite", method))
    {
        out = *f;
        out.n = static_cast<int>(fits.size()); // members, not samples
        out.lo_g = lo;
        out.hi_g = hi;
        // A stand-in is as uncertain as its members, and more so the more they
        // disagree with each other about this mass.
        double member2 = 0;
        for (const Fit *m : fits)
        {
            const double r = m->rel_sd > 0 ? m->rel_sd : kAssumedRelSd;
            member2 += r * r;
        }
        member2 /= fits.size();
        double between2 = 0;
        for (int i = 0; i < kSamples; ++i)
        {
            const double g = lo + (hi - lo) * i / (kSamples - 1);
            double sum = 0, sum2 = 0;
            for (const Fit *m : fits)
            {
                const double v = m->volume_oz(g);
                sum += v;
                sum2 += v * v;
            }
            const double mean = sum / fits.size();
            if (mean > 0)
            {
                between2 += (sum2 / fits.size() - mean * mean) / (mean * mean);
            }
        }
        between2 /= kSamples;
        out.rel_sd = std::sqrt(member2 + between2);
    }
    if (members)
    {
        *members = names;
    }
    return out;
}

CurveChoice choose_curve(const std::string &name, Kind kind, const CurveSet &curves, Method method)
{
    CurveChoice c;
    // Scatter of a fit, or the assumed figure when it has too few points to say.
    auto scatter = [](const Fit &f) { return f.rel_sd > 0 ? f.rel_sd : kAssumedRelSd; };
    if (const Fit *f = curves.fit(name, method))
    {
        c.source = CurveSource::Own;
        c.K = f->K;
        c.p = f->p;
        c.rel_sd = scatter(*f);
        c.rel_se = c.rel_sd / std::sqrt(std::max(1, f->n));
        return c;
    }
    if (const std::string proxy = proxy_curve_for(name, curves); !proxy.empty())
    {
        if (const Fit *f = curves.fit(proxy, method))
        {
            c.source = CurveSource::Family;
            c.K = f->K;
            c.p = f->p;
            c.from = {proxy};
            c.rel_sd = std::hypot(scatter(*f), kBorrowedExtraRelSd);
            c.rel_se = scatter(*f) / std::sqrt(std::max(1, f->n));
            return c;
        }
    }
    std::vector<std::string> members;
    const Fit f = composite_curve(kind, curves, method, &members);
    if (f.valid)
    {
        c.source = CurveSource::Composite;
        c.K = f.K;
        c.p = f.p;
        c.from = members;
        c.rel_sd = scatter(f);
        c.rel_se = c.rel_sd / std::sqrt(std::max(1, f.n));
    }
    return c;
}

std::string describe(const CurveChoice &choice)
{
    switch (choice.source)
    {
    case CurveSource::Own:
        return "own measurements";
    case CurveSource::Family:
        return "borrows " + join(choice.from, ", ");
    case CurveSource::Composite:
        return "composite of " + join(choice.from, ", ");
    case CurveSource::Flat:
        return "flat rate (nothing measured to lean on)";
    }
    return {};
}

BowlItem make_item(const Ingredient &ingredient,
                   const CurveSet &curves,
                   Method method,
                   double flat_protein_rate,
                   double flat_topping_rate)
{
    BowlItem item;
    item.name = ingredient.name;
    item.kind = ingredient.kind;
    item.start_g = ingredient.full_portion();
    item.step_g = ingredient.step_increment_g;
    item.max_g = ingredient.max_dispense_weight_g;
    item.flat_oz_per_100g = ingredient.kind == Kind::Protein ? flat_protein_rate : flat_topping_rate;
    item.piece_height_mm = ingredient.piece_height_mm;

    const CurveChoice c = choose_curve(ingredient.name, ingredient.kind, curves, method);
    if (c.source != CurveSource::Flat)
    {
        item.has_curve = true;
        item.K = c.K;
        item.p = c.p;
    }
    item.rel_sd = c.rel_sd;
    item.curve_rel_se = c.rel_se;
    return item;
}

} // namespace bowlfill
