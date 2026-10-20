#pragma once

#include <algorithm>

#include "curves.hh"
#include "menu_model.hh"

#include <optional>
#include <string>
#include <vector>

namespace bowlfill
{

///
/// One ingredient as it enters the bowl. Dispense order is also stacking order:
/// bases first, then proteins, then toppings.
///
///
/// Uncertainty, for turning one predicted bowl into a range and a probability.
///
/// Three things make a real bowl differ from its prediction: the food lands
/// differently each time (each curve's own scatter, Fit::rel_sd), the curve itself is
/// only estimated (smaller with more data), and the robot misses its target weight.
/// Each is held as a share of the ingredient's volume and the ingredients are taken
/// as independent, so variances add.
///
/// A curve with too few points to measure its own scatter.
constexpr double kAssumedRelSd = 0.15;
/// A flat per-100 g rate is a guess, not a measurement.
constexpr double kFlatRateRelSd = 0.35;
/// A relative standing in (White Jasmine Rice on white rice): its own scatter, plus
/// this much again for not being the same food.
constexpr double kBorrowedExtraRelSd = 0.10;

/// How far the robot misses its target weight, one standard deviation as a share of
/// the target, by kind. The defaults are production data: 274k BowlStation dispenses,
/// 2026-07-14 to 2026-10-12 (product_dispense_metrics, puck scale against target),
/// sauces, packaging, test items and failed dispenses left out. Two ingredients with
/// a target or calibration problem are left out too, as they would swamp the rest:
/// spinach (base, -45% bias) and frozen peas (topping, 70% SD).
struct DispenseVariation
{
    double base = 0.10;    ///< ex-spinach 10.3%; rice alone is ~4-7%
    double protein = 0.12; ///< 12.2%
    double topping = 0.19; ///< ex-peas 19.4%

    /// The average miss, as a share of the target: the robot runs slightly heavy, so
    /// real bowls are centred a little above what was asked for. Same data, means.
    double base_bias = 0.025;    ///< ex-spinach +2.5%
    double protein_bias = 0.032; ///< +3.2%
    double topping_bias = 0.05;  ///< ex-peas +5.0%

    double for_kind(Kind kind) const;
    double bias_for(Kind kind) const;
};

struct BowlItem
{
    std::string name;
    Kind kind = Kind::Topping;
    double start_g = 0.0;
    double final_g = 0.0; ///< filled in by simulate()
    double step_g = 0.0;
    double max_g = 0.0;

    /// Bases carry a fitted power law; everything else has no measurements and
    /// takes a flat rate.
    bool has_curve = false;
    double K = 0.0;
    double p = 1.0;
    double flat_oz_per_100g = 4.5;

    /// Height of one piece, for chunky ingredients that stand proud of the surface
    /// rather than smearing into it. 0 means the ingredient smears.
    double piece_height_mm = 0.0;
    /// How steeply it piles when mounding is on; below 0 takes the bowl's default.
    double mound_slope_deg = -1.0;
    /// Rolls off food steeper than this when it lands; below 0, stays where it lands.
    double roll_off_deg = -1.0;

    /// Bowl-to-bowl scatter around the curve, and the uncertainty of the curve
    /// itself, each as a share of volume (see kAssumedRelSd and friends).
    double rel_sd = kAssumedRelSd;
    double curve_rel_se = 0.0;

    double volume_oz(double grams) const;
    double marginal_oz_per_100g(double grams) const;
};

struct Frame
{
    double grams = 0.0;
    double ounces = 0.0;
    double ounces_uncompressed = 0.0;
    std::vector<double> per_item_oz;
    std::vector<double> per_item_oz_uncompressed;
};

///
/// Sauce rides in a cup inside the bowl and takes real space, so it comes off the
/// capacity available to food. Every bowl carries one; the second is an upcharge.
///
struct SauceCups
{
    static constexpr int kMax = 2;

    /// Two is the common order: one comes with the bowl, the second is an upcharge.
    int cups = 2;
    /// Space one lidded cup takes, as volume-vision reads it (measured 2026-10-12: ~70
    /// ml; the cup's own frustum, 30 mm tall, 65 mm across the lid and 45 mm across
    /// the base, works out to 72 ml).
    double cup_ml = 70.0;

    double volume_oz() const;
};

///
/// The bowl as a vessel under a lid, for judging fit by height rather than volume.
///
/// Smearing food (rice, greens, most toppings) flows into whatever space there is, so
/// for it only volume matters. Two things do not flow: rigid sauce cups and chunky
/// pieces. Each claims height below the lid: a pressed-in cup displaces the food
/// round it, and a chunk standing proud of the surface needs the lid to clear it.
///
/// Measured 2026-10-12: the bowl is 46 mm deep inside (51 mm outside), 161 mm across
/// the bottom and 181 mm across the rim, which holds 1058 ml to the rim. The lid's
/// inside rises 5 mm over a 160 mm plateau in its centre, about 100 ml more. The
/// functional fill (the bowl capacity, 32 oz) is a fill line below the rim: 946 ml
/// reaches 41.6 mm.
///
struct BowlGeometry
{
    bool enabled = false;                   ///< off: the volume-only model, cups at their contents
    double depth_mm = 46.0;                 ///< inside, to the rim
    double top_diameter_mm = 181.0;         ///< inside, at the rim
    double bottom_diameter_mm = 161.0;      ///< inside, at the floor
    double lid_headroom_mm = 5.0;           ///< how far the lid's centre rises over the rim
    double lid_plateau_diameter_mm = 160.0; ///< across that raised centre

    /// Volume below a level surface `height_mm` above the floor, in mm³: the tapered
    /// bowl to the rim, then the lid's plateau above it.
    double volume_below_mm3(double height_mm) const;
    /// The height a volume reaches; the inverse of volume_below_mm3.
    double height_of_mm3(double volume_mm3) const;

    double rim_ml() const
    {
        return volume_below_mm3(depth_mm) / 1000.0;
    }

    /// The lid's inside at its centre, measured from the floor.
    double lid_mm() const
    {
        return depth_mm + std::max(0.0, lid_headroom_mm);
    }

    /// Pressed in, the robot pushes each cup down and the food flows round it, so a
    /// cup costs only what it displaces. Resting, cups sit on the food as dispensed,
    /// so the whole surface must stay a cup height below the lid.
    bool cups_pressed = true;
    /// The cup is a frustum, wide at the lid and narrow at the base. The diameters
    /// only matter when cups are pressed in. Measured 2026-10-12.
    double cup_height_mm = 30.0;        ///< lidded
    double cup_diameter_mm = 65.0;      ///< across the lid
    double cup_base_diameter_mm = 45.0; ///< across the base

    /// Volume of the bottom `height_mm` of a cup, in mm³: what a pressed-in cup
    /// displaces when the food surface sits that far above its base.
    double cup_volume_below_mm3(double height_mm) const;

    /// How much of a chunk's height stands above the smeared surface around it.
    double chunk_proud = 0.5;

    /// Mounding: food dropped at the centre piles up rather than levelling out. With
    /// it on, the surface is a cone of this slope (the food's angle of repose) centred
    /// in the bowl, cut off by the walls: a small portion is a heap on the floor, a
    /// large one fills the bowl with a peak in the middle. It changes how the bowl
    /// looks and how high it peaks, not whether it fits: the lidder presses a mound
    /// down to seal, so fit is still judged on volume.
    bool mound = false;
    double mound_slope_deg = 25.0; ///< the default; ingredients may set their own
    static constexpr double kPlaceholderMoundSlope = 25.0;

    /// Placeholder, until chunks are measured; the UI flags this value.
    static constexpr double kPlaceholderChunkProud = 0.5;
};

/// Space left for food, in oz. With geometry off this is the capacity less the cups'
/// contents. With it on, food may rise to the fill line the capacity reaches, unless
/// the tallest chunk or the cups need it lower to clear the lid, less what pressed-in
/// cups displace.
double food_capacity_oz(double bowl_capacity_oz,
                        const SauceCups &sauce,
                        const BowlGeometry &geometry,
                        double chunk_height_mm);

/// Food that brings the surface to the rim, in oz: the bowl to its rim less the cups
/// (what pressed-in cups displace with geometry on, their contents with it off).
double rim_food_oz(const SauceCups &sauce, const BowlGeometry &geometry);

/// Height of the food's surface above the floor, in mm, with `food_oz` in the bowl:
/// through the taper, around pressed-in cups (with geometry on), and into the lid's
/// raised centre. Past the lid it carries on as if the lid were not there, so an
/// overfull bowl still has a height to draw.
double food_surface_mm(const SauceCups &sauce, const BowlGeometry &geometry, double food_oz);

///
/// Mounding, layer by layer. Each ingredient lands at the centre and piles at its own
/// slope on top of everything already in the bowl: its surface is the higher of the
/// pile below and a cone of its slope, cut off by the walls. A steep ingredient on a
/// flat one makes a peak; a flat one on a steep one fills in round its flanks. One with
/// a roll-off angle first rolls outward until the ground is no steeper than that, and
/// piles there instead: a ridge round the foot of the mound rather than a cone on top.
///
struct MoundLayer
{
    double volume_mm3 = 0.0;
    double slope_deg = 0.0;
    /// A chunky layer rolls outward from the centre over anything steeper than this,
    /// and piles where it comes to rest: a ring at the foot of the mound. Below 0 it
    /// stays where it lands.
    double roll_off_deg = -1.0;
};

/// The radii, in mm from the centre out to the rim, at which mound surfaces are given.
std::vector<double> mound_radii(const BowlGeometry &geometry);

/// The surface after each layer, as heights above the floor at mound_radii(). The
/// first is the empty bowl itself (its floor and walls), so there is one more surface
/// than layers.
std::vector<std::vector<double>> mound_surfaces(const BowlGeometry &geometry, const std::vector<MoundLayer> &layers);

/// The layers of a bowl: each item's volume (`item_oz`, one per item, times `scale`)
/// at its own slope, or the bowl's default where it has none. Empty items are skipped.
std::vector<MoundLayer> mound_layers(const std::vector<BowlItem> &items,
                                     const std::vector<double> &item_oz,
                                     const BowlGeometry &geometry,
                                     double scale = 1.0);

/// The highest point of food among mound_surfaces(): a ridge can stand above the
/// centre, and the bowl's own walls do not count.
double highest_food_mm(const std::vector<std::vector<double>> &surfaces);

/// Height of the bowl's highest point of food once every layer is in.
double mound_peak_mm(const BowlGeometry &geometry, const std::vector<MoundLayer> &layers);

/// Height of the tallest chunk in a bowl, from the items with any mass in it.
double tallest_chunk_mm(const std::vector<BowlItem> &items);

struct SimSettings
{
    double floor_g = 0.0;
    double bowl_capacity_oz = 32.0;
    SauceCups sauce;
    BowlGeometry geometry;
    bool compress = false;
    double load_transfer = 1.0; ///< share of the weight above a base that bears on it
    DispenseVariation dispense;

    /// Tallest chunk in the bowl; simulate() fills this from the items it is given.
    double chunk_height_mm = 0.0;

    /// What is left for food once the cups are in. The ramp is judged against this.
    double food_capacity_oz() const;

    /// Everything that is not food: the cups, and with geometry on, the clearance
    /// the cups and chunks need. capacity = food room + overhead.
    double overhead_oz() const
    {
        return bowl_capacity_oz - food_capacity_oz();
    }

    /// Food that reaches the rim, for the chance of going over it.
    double rim_oz() const
    {
        return rim_food_oz(sauce, geometry);
    }
};

enum class Verdict
{
    NoFloor,          ///< nothing ramps: no rule matched, or the menu sets no floors
    FitsMassBound,    ///< cleared the floor and stayed inside the bowl
    Saturated,        ///< every ingredient hit its max before the floor
    OverAtStart,      ///< the ordered portions alone exceed the bowl
    OverWhileRamping, ///< crossed the volume limit on the way to the floor
};

std::string to_string(Verdict v);

///
/// A predicted bowl as a distribution rather than one number: its median, the range
/// 80% of real bowls should fall in, and the chance of going over each limit.
///
struct FillUncertainty
{
    bool valid = false;
    double median_oz = 0.0;
    double sd_oz = 0.0;
    double p10_oz = 0.0, p90_oz = 0.0; ///< the 80% range
    double room_oz = 0.0;              ///< room left for food (cups, chunks)
    double rim_oz = 0.0;               ///< the bowl to the rim
    double p_over_room = 0.0;
    double p_over_rim = 0.0;
};

/// z for the 10th and 90th percentiles of a normal distribution.
constexpr double kZ80 = 1.2815515655446004;

///
/// The distribution of a bowl whose items predict `item_oz` (one per item, after any
/// squash). Pass `dispense` to include the robot missing its targets, both its scatter
/// and its average lean; leave it null when the masses are known (a weighed bowl).
/// The sum of independent items is taken as normal.
///
FillUncertainty fill_uncertainty(const std::vector<BowlItem> &items,
                                 const std::vector<double> &item_oz,
                                 double room_oz,
                                 double rim_oz,
                                 const DispenseVariation *dispense);

struct SimResult
{
    std::vector<BowlItem> items; ///< each carries start_g and final_g
    std::vector<Frame> frames;   ///< one per pass, frames.front() is as-ordered
    SimSettings settings;
    Verdict verdict = Verdict::NoFloor;

    int passes = 0;
    std::optional<double> crossed_at_g; ///< mass where volume first exceeded the cap
    std::optional<double> highest_safe_floor_g;
    bool saturated = false;
    FillUncertainty fill; ///< the final bowl as a distribution

    const Frame &first() const
    {
        return frames.front();
    }

    const Frame &last() const
    {
        return frames.back();
    }

    double grams_per_pass() const;
    double base_share_of_step() const;
    double squeezed_oz() const;
};

///
/// Mirrors maybe_apply_dynamic_portion_alogithm: every ingredient still under its
/// max takes one step per pass; stop when nothing moved or the total clears the
/// floor. The early-out is strictly greater than the floor, as in the C++.
///
SimResult simulate(std::vector<BowlItem> items, const SimSettings &settings);

/// The bowl's volume with each item at `grams` (one per item): what simulate() and
/// volume_fill() both measure, compression included when the settings ask for it.
Frame bowl_frame(const std::vector<BowlItem> &items, const std::vector<double> &grams, const SimSettings &settings);

///
/// Volume Fill: the dynamic ramp made to respect the bowl. It works in the same
/// step_g passes the robot does, but against volume alone; the weight floor plays
/// no part, so it recommends the same bowl whether or not one is set:
///   - a bowl that fits as written is topped up, every ingredient (proteins
///     included) taking a step per pass toward its max, while the whole next pass
///     still fits; it stops one pass short rather than overfill;
///   - a bowl over as written has its bases and toppings stepped down, a step per
///     pass, until it fits. Proteins are never stepped down, and nothing goes below
///     `min_share` of its recipe portion. An ingredient with no step_g is fixed.
/// "Fits" means within `fit_share` of the food room.
///
struct VolumeFillSettings
{
    double fit_share = 1.0; ///< of the food room
    double min_share = 0.7; ///< of each base's and topping's recipe portion
};

enum class VolumeFillStatus
{
    FilledToLimit, ///< topped up until the next pass would not fit
    AtMax,         ///< everything at its max and still under the limit
    SteppedDown,   ///< over as written, stepped down until it fits
    StillOver,     ///< over even with bases and toppings at their lowest
};

std::string to_string(VolumeFillStatus s);

struct VolumeFillResult
{
    SimResult sim; ///< frames run as-written, then each pass taken
    VolumeFillStatus status = VolumeFillStatus::FilledToLimit;
    double limit_oz = 0.0; ///< the volume it had to fit
    int passes_up = 0, passes_down = 0;
};

VolumeFillResult volume_fill(std::vector<BowlItem> items, const SimSettings &settings, const VolumeFillSettings &vf);

///
/// apply_proportional_reduction_to_product: halve each base, then floor at its
/// configured minimum. Only fires when two bases are present.
///
double split_base_start(const Ingredient &ingredient, bool split_on, bool two_bases);

/// Volume per 100 g for the ingredients with no measured curve. Nothing here was
/// weighed; they stand in until proteins and toppings are run through the rig.
constexpr double kFlatProteinRate = 3.5;
constexpr double kFlatToppingRate = 4.5;

///
/// Where an ingredient's mass-to-volume curve comes from, best first:
///   Own        its own measurements
///   Family     a measured relative (any rice for White Jasmine Rice; see
///              proxy_curve_for)
///   Composite  a protein or topping with neither: the average of every measured
///              curve of the same kind, so Guajillo Cumin Chicken stands on Chicken
///              Al Pastor and Suadero Beef rather than a flat rate
///   Flat       nothing measured to lean on: the flat per-100 g rate
///
enum class CurveSource
{
    Own,
    Family,
    Composite,
    Flat
};

struct CurveChoice
{
    CurveSource source = CurveSource::Flat;
    double K = 0.0, p = 1.0;
    std::vector<std::string> from;  ///< the curve borrowed, or the composite's members
    double rel_sd = kFlatRateRelSd; ///< bowl-to-bowl scatter, as a share of volume
    double rel_se = 0.0;            ///< uncertainty of the curve itself
};

/// The curve to use for `name`, of kind `kind` (see CurveSource).
CurveChoice choose_curve(const std::string &name, Kind kind, const CurveSet &curves);

///
/// The average curve of every measured ingredient of `kind`, each counted once
/// however many readings it has, refitted as a single power law over the range they
/// span. Curves named "test ..." are calibration references, not food, and bases
/// never get a composite. Invalid when nothing of that kind is measured.
///
Fit composite_curve(Kind kind, const CurveSet &curves, std::vector<std::string> *members = nullptr);

/// One line for the UI: "own curve", "composite of A, B", ...
std::string describe(const CurveChoice &choice);

/// Builds a bowl item from menu configuration plus the fitted curves.
BowlItem make_item(const Ingredient &ingredient,
                   const CurveSet &curves,
                   double flat_protein_rate = kFlatProteinRate,
                   double flat_topping_rate = kFlatToppingRate);

} // namespace bowlfill
