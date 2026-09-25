#pragma once

#include <QString>
#include <QStringList>
#include <map>
#include <vector>

namespace bowlfill {

/// Which fill method a measurement came from. Hand-filled bowls hold about 5.8%
/// more volume at equal mass than robot-dispensed ones (p = 0.002), and the gap is
/// concentrated in the greens, so the arms are fitted separately.
enum class Method { Robot, Hand, Pooled };

QString to_string(Method method);

struct Observation {
    QString ingredient;
    Method method = Method::Robot;
    double grams = 0.0;
    double ounces = 0.0;
};

///
/// oz = K * g^p, fitted by ordinary least squares on log-log.
///
/// Preferred over a straight line because it passes through the origin: a linear
/// fit needs a 7.3 oz intercept for kale, i.e. volume at zero mass. p < 1 is
/// compaction -- each added gram buys less volume than the one before it.
///
struct Fit {
    double K = 0.0;
    double p = 1.0;
    double r2 = 0.0;
    double lo_g = 0.0;   ///< measured range, for flagging extrapolation
    double hi_g = 0.0;
    int n = 0;
    bool valid = false;

    double volume_oz(double grams) const;
    /// d(oz)/dg expressed per 100 g: what a ramp step actually costs the bowl.
    double marginal_oz_per_100g(double grams) const;
};

///
/// The measured mass-to-volume dataset. Ships with the 36 hand-measured bowls and
/// accepts more from CSV, refitting the affected ingredients.
///
class CurveSet {
public:
    /// Expected columns, case-insensitive, in any order. `method` is optional and
    /// defaults to robot.
    static QStringList required_columns();
    static QStringList optional_columns();
    static QString format_help();
    static const double kMlPerFlOz;

    bool load_csv(const QString &path, QString *error, int *rows_added = nullptr,
                  QStringList *new_ingredients = nullptr);
    void add(const Observation &obs);
    void refit();
    void reset_to_builtin(const QString &builtin_csv_path);

    const Fit *fit(const QString &ingredient, Method method) const;
    QStringList ingredients() const;
    const std::vector<Observation> &observations() const { return obs_; }
    int count(const QString &ingredient, Method method) const;

private:
    std::vector<Observation> obs_;
    std::map<QString, std::map<Method, Fit>> fits_;
};

/// Bases with no measurements of their own borrow the closest measured curve.
/// Returns an empty string when the ingredient has its own data.
QString proxy_curve_for(const QString &ingredient, const CurveSet &curves);

}  // namespace bowlfill
