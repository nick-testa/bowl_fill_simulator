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

    double volume_oz(double grams) const;
    /// d(oz)/dg expressed per 100 g: what a ramp step actually costs the bowl.
    double marginal_oz_per_100g(double grams) const;
};

///
/// The measured mass-to-volume dataset, refitting the affected ingredients as
/// measurements are added.
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

    const Fit *fit(const std::string &ingredient) const;
    std::vector<std::string> ingredients() const;

    const std::vector<Observation> &observations() const
    {
        return obs_;
    }

    int count(const std::string &ingredient) const;

private:
    std::vector<Observation> obs_;
    std::map<std::string, Fit> fits_;
    /// Names match regardless of case ("white rice" from a bowl sheet is the menu's
    /// "White Rice"); lower-cased name -> the spelling the curve is shown under.
    std::map<std::string, std::string> display_;
    FitSpace space_ = FitSpace::Linear;
};

/// Bases with no measurements of their own borrow the closest measured curve.
/// Returns an empty string when the ingredient has its own data.
std::string proxy_curve_for(const std::string &ingredient, const CurveSet &curves);

} // namespace bowlfill
