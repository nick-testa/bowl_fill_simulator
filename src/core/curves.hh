#pragma once

#include <map>
#include <string>
#include <vector>

namespace bowlfill
{

/// One robot-dispensed bowl of a single ingredient. Hand-filled measurements are no
/// longer used: hand bowls held about 5.8% more volume, and production is robot-filled.
struct Observation
{
    std::string ingredient;
    double grams = 0.0;
    double ounces = 0.0;
    /// Where it came from: the measurement file, which stands for one batch of food.
    std::string batch;
};

///
/// oz = K * g^p. Preferred over a straight line because it passes through the
/// origin: a linear fit needs a 7.3 oz intercept for kale, i.e. volume at zero mass.
/// p < 1 is compaction -- each added gram buys less volume than the one before it.
///
/// Fitted by least squares on the volume itself (FitSpace::Linear, the default).
/// The log-log fit the HTML simulator used weights every point by its relative
/// error, so a 4 ml reading counts as much as a 400 ml one; the camera's error is
/// a few ml whatever the fill, so its smallest readings bent those curves upward.
///
enum class FitSpace
{
    Linear,
    Log
};

struct Fit
{
    double K = 0.0;
    double p = 1.0;
    double r2 = 0.0;
    double lo_g = 0.0; ///< measured range, for flagging extrapolation
    double hi_g = 0.0;
    int n = 0;
    bool valid = false;
    /// How far single bowls land from the curve, as a share of its prediction (the
    /// standard deviation of reading / curve - 1). Romaine's is large: leaves land
    /// differently each time. -1 when there are too few points to say (n <= 2).
    double rel_sd = -1.0;
    /// From the fitted (Bayesian) curves rather than least squares on this ingredient alone.
    bool fitted = false;
    /// For a fitted curve: how far one bowl from a batch never measured lands from the
    /// curve, as a share of its prediction. Batch-to-batch variation, the curve's own
    /// uncertainty and bowl scatter together; -1 when not fitted.
    double new_batch_rel_sd = -1.0;

    double volume_oz(double grams) const;
    /// d(oz)/dg expressed per 100 g: what a ramp step actually costs the bowl.
    double marginal_oz_per_100g(double grams) const;
};

///
/// One curve from data/fitted_curves.json, written by tools/fit_curves.py: a measured
/// ingredient's posterior curve, or a pool group's typical curve for an ingredient
/// with no measurements of its own.
///
struct FittedCurve
{
    std::string name;
    std::string group;
    double K = 0.0; ///< fl oz at 1 g
    double p = 1.0;
    int n = 0;       ///< measured bowls (for a group: measured members)
    int batches = 0; ///< measurement files they came from
    double lo_g = 0.0, hi_g = 0.0;
    double scatter_rel_sd = -1.0;   ///< bowl to bowl within one batch
    double new_batch_rel_sd = -1.0; ///< one bowl from a new batch: batch + curve + scatter
};

/// Everything a fit wrote. Empty (`loaded` false) until a file is read.
struct FittedCurves
{
    bool loaded = false;
    std::string fingerprint; ///< of the fit input it was fitted on; see fit_input_csv()
    std::string fitted_at;
    double batch_rel_sd = 0.0; ///< how much a new batch typically shifts volume
    int divergences = 0;
    double max_rhat = 0.0;
    std::vector<FittedCurve> ingredients;
    std::vector<FittedCurve> groups;
};

///
/// The measured mass-to-volume dataset, refitting the affected ingredients as
/// measurements are added. With fitted curves set, those are what fit() returns; least
/// squares on an ingredient's own points is the fallback for anything the fit does not
/// cover (no fitted file yet, or measurements added since).
///
class CurveSet
{
public:
    /// Expected columns, case-insensitive, in any order. `method` is optional; rows
    /// marked `hand` are skipped.
    static std::vector<std::string> required_columns();
    static std::vector<std::string> optional_columns();
    static std::string format_help();
    static const double kMlPerFlOz;

    bool load_csv(const std::string &path,
                  std::string *error,
                  int *rows_added = nullptr,
                  std::vector<std::string> *new_ingredients = nullptr,
                  int *hand_rows_skipped = nullptr);
    void add(const Observation &obs);
    void refit();
    void clear();
    /// How curves are fitted; refits when it changes. Log reproduces the HTML
    /// simulator's published fits.
    void set_fit_space(FitSpace space);

    FitSpace fit_space() const
    {
        return space_;
    }

    void reset_to_builtin(const std::string &builtin_csv_path);

    /// Use these fitted curves; an empty set returns to least squares throughout.
    void set_fitted(FittedCurves fitted);

    const FittedCurves &fitted() const
    {
        return fitted_;
    }

    /// Pool groups set by hand (data/ingredient_groups.csv); unlisted ingredients take
    /// their automatic group.
    void set_groups(std::map<std::string, std::string> groups);

    const std::map<std::string, std::string> &groups() const
    {
        return groups_;
    }

    std::string group_of(const std::string &ingredient) const;
    /// A pool group's typical curve, for an ingredient with nothing measured; nullptr
    /// without fitted curves or when nothing in the group has been measured.
    const Fit *group_fit(const std::string &group) const;

    const Fit *fit(const std::string &ingredient) const;
    std::vector<std::string> ingredients() const;

    const std::vector<Observation> &observations() const
    {
        return obs_;
    }

    int count(const std::string &ingredient) const;

private:
    std::vector<Observation> obs_;
    std::map<std::string, Fit> fits_;           ///< least squares, by display name
    FittedCurves fitted_;
    std::map<std::string, Fit> fitted_fits_;    ///< by lower-cased name
    std::map<std::string, Fit> group_fits_;     ///< by lower-cased group
    std::map<std::string, std::string> groups_;
    /// Names match regardless of case ("white rice" from a bowl sheet is the menu's
    /// "White Rice"); lower-cased name -> the spelling the curve is shown under.
    std::map<std::string, std::string> display_;
    FitSpace space_ = FitSpace::Linear;
};

/// Bases with no measurements of their own borrow the closest measured curve.
/// Returns an empty string when the ingredient has its own data.
std::string proxy_curve_for(const std::string &ingredient, const CurveSet &curves);

} // namespace bowlfill
