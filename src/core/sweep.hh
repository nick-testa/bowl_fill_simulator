#pragma once

#include "feasibility.hh"

#include <QString>
#include <functional>
#include <vector>

class QTextStream;

namespace bowlfill {

///
/// The combinatorial workup: every bowl a brand's menu can actually produce, run
/// through both dispense models.
///
/// A row is one named recipe served over one ingredient combination. The recipe
/// contributes its pinned gram weights wherever the combination still contains the
/// ingredient it pinned; everything substituted in falls back to that ingredient's own
/// menu portion. A customer-built shell pins nothing, so it sweeps the whole space.
///
/// The 50:50 base split and base compression are forced on for every swept bowl:
/// they are the operative hardware behaviour, not a comparison knob.
/// Only bowls a customer could actually order are swept. The menu requires a base, a
/// topping and a sauce; a second base, a second protein and a second sauce are all
/// available, and the extra protein is optional in price but never absent in practice.
struct SweepLimits {
    int min_bases = 1, max_bases = 2;
    int min_proteins = 1, max_proteins = 2;
    int min_toppings = 1;
    /// Topping subsets are the term that explodes: 20 toppings is a million subsets
    /// before anything else is multiplied in. Capping the count also drops bowls no
    /// customer would order.
    int max_toppings = 4;
    /// One sauce comes with the bowl, the second is an upcharge. Both are swept.
    int min_sauces = 1, max_sauces = SauceCups::kMax;
};

/// Ingredient combinations a brand admits under these limits, before recipes.
qint64 combinations_for(const Menu &menu, const SweepLimits &limits);

struct SweepPlan {
    QString brand;
    qint64 combinations = 0;
    int recipes = 0;

    qint64 rows() const { return combinations * recipes; }
    /// Rough CSV size, for warning the user before they commit to a 20 GB file.
    qint64 bytes() const { return rows() * 185; }
};

SweepPlan plan_sweep(const Menu &menu, const SweepLimits &limits);

struct SweepTotals {
    qint64 rows = 0;
    qint64 legacy_fits = 0, legacy_over = 0, legacy_short = 0;
    /// StillOver means "cannot be brought down to the fill target", which is not the
    /// same as not fitting: a bowl held at 90% is both. `adaptive_over_bowl` is the
    /// count that actually overflows.
    qint64 adaptive_ok = 0, adaptive_over = 0, adaptive_short = 0;
    qint64 adaptive_over_bowl = 0;

    double legacy_pass_rate() const { return rows ? double(legacy_fits) / rows : 0.0; }
};

QString sweep_csv_header();

///
/// Streams every row to `out`. `progress(done, total)` is called periodically and
/// returns false to cancel, in which case the sweep stops and this returns false.
///
bool run_sweep(const std::vector<const Menu *> &menus, const CurveSet &curves,
               Method method, const SimSettings &legacy_settings,
               const AdaptiveSettings &adaptive_settings, const CostTable *costs,
               const SweepLimits &limits, QTextStream &out,
               const std::function<bool(qint64, qint64)> &progress,
               std::vector<SweepTotals> *per_brand = nullptr);

}  // namespace bowlfill
