#pragma once

#include <QString>

///
/// Display units for volume.
///
/// The core works entirely in US fluid ounces: the measured data is recorded that way
/// and every fitted K is expressed in it. Converting inside the model would mean
/// re-deriving the curves, so conversion happens only here, at the point of display.
/// Anything a user types is converted straight back before it reaches the model.
///
namespace bowlfill::units {

enum class Volume { FluidOunces, Millilitres };

constexpr double kMlPerFlOz = 29.5735295625;   // US fluid ounce, exact by definition

void set(Volume v);
Volume current();
bool metric();

/// Canonical fluid ounces to whatever is being shown, and back.
double from_oz(double oz);
double to_oz(double shown);

QString suffix();                ///< "oz" or "ml"
QString volume_axis_label();     ///< for the ramp chart's y axis
QString curve_axis_label();      ///< for the measured-curve chart's y axis

/// A total or a per-ingredient volume. Ounces get a decimal; millilitres do not,
/// because 845 ml carries the same precision as 28.6 oz without the noise.
QString volume(double oz, bool with_suffix = false);

/// A marginal rate in volume per 100 g.
QString rate(double oz_per_100g, bool with_suffix = false);

/// The K of oz = K*g^p, restated in the display unit.
double scale_k(double k_oz);

}  // namespace bowlfill::units
