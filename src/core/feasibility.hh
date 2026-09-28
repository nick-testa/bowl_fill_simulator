#pragma once

#include "adaptive.hh"
#include "curves.hh"
#include "menu_model.hh"
#include "simulator.hh"

#include <QString>
#include <vector>

namespace bowlfill {

///
/// Builds the bowl a recipe describes, in dispense order: bases, then proteins, then
/// toppings. Shared by the UI, the audit and the tests so all three agree on what a
/// recipe means.
///
std::vector<BowlItem> bowl_from_recipe(const Menu &menu, const Recipe &recipe,
                                       const CurveSet &curves, Method method,
                                       double flat_protein_rate = 3.5,
                                       double flat_topping_rate = 4.5);

/// One row of the per-brand feasibility audit.
struct RecipeAudit {
    QString recipe;
    int pinned = 0;            ///< ingredients the template fixes
    bool customer_built = false;
    bool complete_bowl = false; ///< pins enough to be judged as a whole bowl

    double nominal_oz = 0.0;
    double nominal_g = 0.0;
    double floor_g = 0.0;
    bool floor_matched = false;

    SimResult legacy;
    AdaptiveResult adaptive;

    /// A short, plain description of what goes wrong, or that nothing does.
    QString legacy_summary() const;
    QString adaptive_summary() const;
    /// Ordering for the report: worst first.
    int severity() const;
    bool legacy_ok() const;
};

struct BrandAudit {
    QString brand;
    std::vector<RecipeAudit> rows;
    int complete = 0, partial = 0, customer_built = 0;

    /// Overflowing and never filling are different problems with different fixes, so
    /// they are counted apart rather than summed into one "fails" number.
    int legacy_over = 0, legacy_short = 0;
    int adaptive_over = 0, adaptive_short = 0;

    int legacy_failing() const { return legacy_over + legacy_short; }

    QString headline() const;
    QString to_csv() const;
};

BrandAudit audit_brand(const Menu &menu, const CurveSet &curves, Method method,
                       const SimSettings &legacy_settings,
                       const AdaptiveSettings &adaptive_settings,
                       const CostTable *costs = nullptr);

}  // namespace bowlfill
