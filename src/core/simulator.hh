#pragma once

#include "curves.hh"
#include "menu_model.hh"

#include <QString>
#include <optional>
#include <vector>

namespace bowlfill {

///
/// One ingredient as it enters the bowl. Dispense order is also stacking order:
/// bases first, then proteins, then toppings.
///
struct BowlItem {
    QString name;
    Kind kind = Kind::Topping;
    double start_g = 0.0;
    double final_g = 0.0;  ///< filled in by simulate()
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

    double volume_oz(double grams) const;
    double marginal_oz_per_100g(double grams) const;
};

struct Frame {
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
struct SauceCups {
    static constexpr int kMax = 2;

    /// Two is the common order: one comes with the bowl, the second is an upcharge.
    int cups = 2;
    double cup_ml = 50.0;

    double volume_oz() const;
};

///
/// The bowl as a vessel under a lid, for judging fit by height rather than volume.
///
/// Smearing food (rice, greens, most toppings) flows into whatever space there is, so
/// for it only volume matters. Two things do not flow: rigid sauce cups and chunky
/// pieces. Each claims height below the lid, and height in a near-cylindrical bowl is
/// volume: every millimetre of clearance they need is a millimetre of food the bowl
/// cannot hold across its whole footprint (for chunks) or under the cup (for cups).
///
/// The footprint is taken from the capacity over the depth, not from a measured
/// diameter: 32 oz is the functional full level at the rim, and a measured diameter
/// may carry error, so the capacity is trusted and the bowl treated as a cylinder.
///
struct BowlGeometry {
    bool enabled = false;          ///< off: the volume-only model, cups at their contents
    double depth_mm = 44.5;        ///< rim height, where the functional capacity is reached
    double lid_headroom_mm = 0.0;  ///< extra clearance at the centre of a domed lid

    /// Pressed in, the robot pushes each cup down and the food flows round it, so a
    /// cup costs only its own column. Resting, cups sit on the food as dispensed, so
    /// the whole surface must stay a cup height below the lid.
    bool cups_pressed = true;
    double cup_height_mm = 35.0;   ///< lidded
    double cup_diameter_mm = 62.0; ///< only used when pressed in

    /// How much of a chunk's height stands above the smeared surface around it.
    double chunk_proud = 0.5;

    /// Placeholders, until the cup and lid are measured; the UI flags these values.
    static constexpr double kPlaceholderCupHeight = 35.0;
    static constexpr double kPlaceholderCupDiameter = 62.0;
    static constexpr double kPlaceholderHeadroom = 0.0;
    static constexpr double kPlaceholderChunkProud = 0.5;
};

/// Space left for food, in oz. With geometry off this is the capacity less the cups'
/// contents; with it on, less what the cups and the tallest chunk need below the lid.
double food_capacity_oz(double bowl_capacity_oz, const SauceCups &sauce,
                        const BowlGeometry &geometry, double chunk_height_mm);

/// Height of the tallest chunk in a bowl, from the items with any mass in it.
double tallest_chunk_mm(const std::vector<BowlItem> &items);

struct SimSettings {
    double floor_g = 0.0;
    double bowl_capacity_oz = 32.0;
    SauceCups sauce;
    BowlGeometry geometry;
    bool compress = false;
    double load_transfer = 1.0;  ///< share of the weight above a base that bears on it

    /// Tallest chunk in the bowl; simulate() fills this from the items it is given.
    double chunk_height_mm = 0.0;

    /// What is left for food once the cups are in. The ramp is judged against this.
    double food_capacity_oz() const;
    /// Everything that is not food: the cups, and with geometry on, the clearance
    /// the cups and chunks need. capacity = food room + overhead.
    double overhead_oz() const { return bowl_capacity_oz - food_capacity_oz(); }
};

enum class Verdict {
    NoFloor,          ///< nothing ramps: no rule matched, or the menu sets no floors
    FitsMassBound,    ///< cleared the floor and stayed inside the bowl
    Saturated,        ///< every ingredient hit its max before the floor
    OverAtStart,      ///< the ordered portions alone exceed the bowl
    OverWhileRamping, ///< crossed the volume limit on the way to the floor
};

QString to_string(Verdict v);

struct SimResult {
    std::vector<BowlItem> items;   ///< each carries start_g and final_g
    std::vector<Frame> frames;     ///< one per pass, frames.front() is as-ordered
    SimSettings settings;
    Verdict verdict = Verdict::NoFloor;

    int passes = 0;
    std::optional<double> crossed_at_g;    ///< mass where volume first exceeded the cap
    std::optional<double> highest_safe_floor_g;
    bool saturated = false;

    const Frame &first() const { return frames.front(); }
    const Frame &last() const { return frames.back(); }
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
enum class CurveSource { Own, Family, Composite, Flat };

struct CurveChoice {
    CurveSource source = CurveSource::Flat;
    double K = 0.0, p = 1.0;
    QStringList from;   ///< the curve borrowed, or the composite's members
};

/// The curve to use for `name`, of kind `kind` (see CurveSource).
CurveChoice choose_curve(const QString &name, Kind kind, const CurveSet &curves, Method method);

///
/// The average curve of every measured ingredient of `kind`, each counted once
/// however many readings it has, refitted as a single power law over the range they
/// span. Curves named "test ..." are calibration references, not food, and bases
/// never get a composite. Invalid when nothing of that kind is measured.
///
Fit composite_curve(Kind kind, const CurveSet &curves, Method method,
                    QStringList *members = nullptr);

/// One line for the UI: "own curve", "composite of A, B", ...
QString describe(const CurveChoice &choice);

/// Builds a bowl item from menu configuration plus the fitted curves.
BowlItem make_item(const Ingredient &ingredient, const CurveSet &curves, Method method,
                   double flat_protein_rate = kFlatProteinRate,
                   double flat_topping_rate = kFlatToppingRate);

}  // namespace bowlfill
