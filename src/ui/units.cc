#include "units.hh"

#include <cmath>

namespace bowlfill::units {
namespace {
Volume g_unit = Volume::FluidOunces;
}

void set(Volume v) { g_unit = v; }
Volume current() { return g_unit; }
bool metric() { return g_unit == Volume::Millilitres; }

double from_oz(double oz) { return metric() ? oz * kMlPerFlOz : oz; }
double to_oz(double shown) { return metric() ? shown / kMlPerFlOz : shown; }

QString suffix() { return metric() ? QStringLiteral("ml") : QStringLiteral("oz"); }

QString volume_axis_label()
{
    return metric() ? QStringLiteral("bowl volume (ml)")
                    : QStringLiteral("bowl volume (fl oz)");
}

QString curve_axis_label()
{
    return metric() ? QStringLiteral("volume (ml)") : QStringLiteral("volume (fl oz)");
}

QString volume(double oz, bool with_suffix)
{
    const double v = from_oz(oz);
    // Drop a trailing ".0" so a round capacity reads "32 oz", not "32.0 oz".
    const int dp = metric() ? 0 : (std::fabs(v - std::round(v)) < 0.05 ? 0 : 1);
    const QString n = QString::number(v, 'f', dp);
    return with_suffix ? n + " " + suffix() : n;
}

QString rate(double oz_per_100g, bool with_suffix)
{
    const QString n = QString::number(from_oz(oz_per_100g), 'f', metric() ? 1 : 2);
    return with_suffix ? n + " " + suffix() : n;
}

double scale_k(double k_oz) { return from_oz(k_oz); }

}  // namespace bowlfill::units
