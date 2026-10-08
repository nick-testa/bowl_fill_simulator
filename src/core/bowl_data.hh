#pragma once

#include "curves.hh"
#include "simulator.hh"

#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace bowlfill {

///
/// Measured bowls, one row per bowl: a column per ingredient holding the grams
/// dispensed (blank when absent), then the scanned volume of the finished bowl.
///
/// Ingredient columns are read left to right as dispense order, which is also
/// stacking order -- the first column is the bottom layer. On the robot that is
/// station order. Column names may be typed as the menu or station spells them
/// ("White Jasmine Rice retherm v11"); they are cleaned the way the menu loader
/// cleans them.
///
/// A row holding a single ingredient is a point on that ingredient's own curve, so a
/// stacked ladder (dispense 20 g, scan, another 20 g, scan, ...) goes in the same
/// sheet with the running total in its column.
///
struct BowlRow {
    std::vector<std::pair<QString, double>> items;   ///< (name, grams), bottom first
    double ounces = 0.0;   ///< scanned volume, converted from ml when given in ml
    int cups = 0;          ///< sauce cups pressed in before the scan
    QString lid;           ///< "closed", "forced", "failed" or empty
    int line = 0;          ///< 1-based line in the file, for messages
};

struct BowlSheet {
    QStringList ingredients;   ///< cleaned column names, in dispense order
    std::vector<BowlRow> rows;
    int skipped = 0;           ///< rows with no usable volume or no ingredient

    int single_rows() const;
    int mixed_rows() const;
};

QString bowl_data_format_help();

///
/// `empty_bowl_ml` is what the camera reads for an empty bowl, its zero error (about
/// -5 ml on the volume-vision rig). Every scanned volume is corrected by it before
/// anything else, so a reading of -3 ml on a few beans counts as +2 ml.
///
bool load_bowl_csv(const QString &path, BowlSheet &out, QString *error = nullptr,
                   double empty_bowl_ml = 0.0);

/// The single-ingredient rows, as curve observations (robot-dispensed).
std::vector<Observation> single_ingredient_points(const BowlSheet &sheet);

///
/// How hard the lower layers are squashed by what is dispensed on top of them, fitted
/// from the mixed bowls. It is the simulator's load_transfer: 0 is no squash at all,
/// 1 is the weight above bearing fully on the bed (the old upper bound from physics
/// alone). Above 1 the real bowls compact more than weight explains -- the lid,
/// cups pressed in and the drop itself -- and the factor is then an empirical fit.
///
struct SquashFit {
    bool valid = false;
    int bowls = 0;                 ///< mixed bowls used
    double load_transfer = 0.0;    ///< the fitted factor
    bool at_bound = false;         ///< pinned at the search limit: the model cannot reach the data
    double rmse_uncompressed_oz = 0.0;  ///< error with no squash
    double rmse_default_oz = 0.0;       ///< error with load transfer 1.0
    double rmse_fitted_oz = 0.0;        ///< error at the fitted factor
    double mean_bias_oz = 0.0;          ///< predicted minus measured, at the fit
    QStringList flat_rate;     ///< ingredients with no curve, carried at a flat rate
};

constexpr double kMaxLoadTransfer = 20.0;

/// Predicted scan volume of one row: food, compressed at `load_transfer`, plus the
/// cups' contents when cups were in for the scan.
double predicted_scan_oz(const BowlRow &row, const CurveSet &curves, Method method,
                         double load_transfer, double flat_protein_rate = kFlatProteinRate,
                         double flat_topping_rate = kFlatToppingRate,
                         QStringList *flat_rate = nullptr);

/// Least-squares fit of load_transfer over the mixed rows. Curves should already
/// include the sheet's single-ingredient points.
SquashFit fit_squash(const BowlSheet &sheet, const CurveSet &curves, Method method,
                     double flat_protein_rate = kFlatProteinRate,
                     double flat_topping_rate = kFlatToppingRate);

}  // namespace bowlfill
