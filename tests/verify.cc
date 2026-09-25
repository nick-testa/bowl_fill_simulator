///
/// Checks the C++ core against values produced by the HTML simulator it was ported
/// from. Any drift here means the port changed behaviour, which is the one thing
/// this rewrite must not do.
///
#include "core/curves.hh"
#include "core/menu_model.hh"
#include "core/simulator.hh"

#include <QString>
#include <QStringList>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace bowlfill;

namespace {

int failures = 0;
int checks = 0;

void expect_near(const QString &what, double got, double want, double tol)
{
    ++checks;
    if (std::fabs(got - want) > tol) {
        ++failures;
        std::printf("  FAIL %-46s got %10.3f  want %10.3f  (tol %g)\n",
                    qPrintable(what), got, want, tol);
    } else {
        std::printf("  ok   %-46s %10.3f\n", qPrintable(what), got);
    }
}

void expect_eq(const QString &what, const QString &got, const QString &want)
{
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("  FAIL %-46s got %-22s want %s\n", qPrintable(what),
                    qPrintable(got), qPrintable(want));
    } else {
        std::printf("  ok   %-46s %s\n", qPrintable(what), qPrintable(got));
    }
}

QString verdict_name(Verdict v)
{
    switch (v) {
    case Verdict::NoFloor: return "NoFloor";
    case Verdict::FitsMassBound: return "Fits";
    case Verdict::Saturated: return "Saturated";
    case Verdict::OverAtStart: return "OverAtStart";
    case Verdict::OverWhileRamping: return "OverWhileRamping";
    }
    return "?";
}

const Menu *by_brand(const std::vector<Menu> &menus, const QString &brand)
{
    for (const Menu &m : menus)
        if (m.brand == brand) return &m;
    return nullptr;
}

/// Builds the bowl a recipe describes, in dispense order, exactly as the UI does.
std::vector<BowlItem> bowl_from_recipe(const Menu &menu, const Recipe &rec,
                                       const CurveSet &curves, Method method)
{
    std::vector<BowlItem> bases, proteins, toppings;
    for (const RecipeItem &ri : rec.items) {
        const Ingredient *ing = menu.find(ri.name);
        if (!ing) continue;
        BowlItem item = make_item(*ing, curves, method, 3.5, 4.5);
        item.start_g = ri.grams;
        (ing->kind == Kind::Base ? bases
                                 : ing->kind == Kind::Protein ? proteins : toppings)
            .push_back(item);
    }
    std::vector<BowlItem> all;
    all.insert(all.end(), bases.begin(), bases.end());
    all.insert(all.end(), proteins.begin(), proteins.end());
    all.insert(all.end(), toppings.begin(), toppings.end());
    return all;
}

QStringList names_in(const std::vector<BowlItem> &items)
{
    QStringList out;
    for (const BowlItem &it : items) out << it.name;
    return out;
}

SimResult run_recipe(const Menu &menu, const QString &recipe_name, const CurveSet &curves,
                     double cap = 32.0)
{
    const Recipe *rec = nullptr;
    for (const Recipe &r : menu.recipes)
        if (r.name == recipe_name) rec = &r;
    if (!rec) {
        std::printf("  FAIL recipe not found: %s\n", qPrintable(recipe_name));
        ++failures;
        ++checks;
        return {};
    }
    std::vector<BowlItem> items = bowl_from_recipe(menu, *rec, curves, Method::Robot);
    SimSettings s;
    s.bowl_capacity_oz = cap;
    if (const FloorRule *f = menu.floor_for(names_in(items)))
        s.floor_g = f->minimum_product_weight_g;
    return simulate(items, s);
}

}  // namespace

int main()
{
    const QString assets = BOWLFILL_ASSET_DIR;

    std::printf("Curve fitting (robot arm, against the published power-law fits)\n");
    CurveSet curves;
    QString err;
    if (!curves.load_csv(assets + "/data/mass_to_volume.csv", &err)) {
        std::printf("  FATAL could not load mass_to_volume.csv: %s\n", qPrintable(err));
        return 1;
    }
    struct { const char *name; double K, p, r2, lo, hi; } expected[] = {
        {"Massaged Kale", 1.106, 0.718, 0.958, 47, 99},
        {"Mexican Rice",  0.298, 0.723, 0.921, 183, 300},
        {"Romaine Base",  0.311, 0.938, 0.983, 67, 141},
    };
    for (const auto &e : expected) {
        const Fit *f = curves.fit(e.name, Method::Robot);
        if (!f) { std::printf("  FAIL no fit for %s\n", e.name); ++failures; ++checks; continue; }
        expect_near(QString("%1 K").arg(e.name), f->K, e.K, 0.002);
        expect_near(QString("%1 p").arg(e.name), f->p, e.p, 0.002);
        expect_near(QString("%1 R2").arg(e.name), f->r2, e.r2, 0.01);
        expect_near(QString("%1 range lo").arg(e.name), f->lo_g, e.lo, 0.01);
        expect_near(QString("%1 range hi").arg(e.name), f->hi_g, e.hi, 0.01);
        expect_near(QString("%1 n").arg(e.name), f->n, 6, 0.01);
    }
    // Hand-filled bowls hold more volume at equal mass; kale is the extreme case.
    if (const Fit *h = curves.fit("Massaged Kale", Method::Hand))
        expect_near("Massaged Kale hand K", h->K, 1.318, 0.002);
    if (const Fit *a = curves.fit("Massaged Kale", Method::Pooled))
        expect_near("Massaged Kale pooled n", a->n, 12, 0.01);

    std::printf("\nMenu loading\n");
    LoadResult loaded = load_menus(assets + "/menus");
    for (const QString &w : loaded.warnings) std::printf("  warn %s\n", qPrintable(w));
    expect_near("brands parsed", loaded.menus.size(), 5, 0.01);

    const Menu *farmstand = by_brand(loaded.menus, "Farmstand");
    const Menu *cowgirl = by_brand(loaded.menus, "The Hungry Cowgirl");
    const Menu *siren = by_brand(loaded.menus, "The Hungry Siren");
    const Menu *pita = by_brand(loaded.menus, "Pita Dust");
    const Menu *meatrice = by_brand(loaded.menus, "Meat + Rice");
    if (!farmstand || !cowgirl || !siren || !pita || !meatrice) {
        std::printf("  FATAL a brand is missing\n");
        return 1;
    }

    expect_near("Farmstand floors", farmstand->floors.size(), 4, 0.01);
    expect_near("Farmstand ingredients", farmstand->ingredients.size(), 28, 0.01);
    expect_near("Farmstand recipes", farmstand->recipes.size(), 10, 0.01);
    expect_near("Meat + Rice floors", meatrice->floors.size(), 0, 0.01);
    expect_near("Meat + Rice recipes", meatrice->recipes.size(), 1, 0.01);

    // Portion resolution: romaine must read its standalone (build-your-own) weight,
    // not the 100 g a preconfigured salad is plated at.
    expect_near("Farmstand romaine portion", farmstand->find("Romaine Base")->full_portion(),
                35, 0.01);
    expect_near("Farmstand romaine step", farmstand->find("Romaine Base")->step_increment_g,
                3.5, 0.001);
    expect_near("Farmstand romaine max",
                farmstand->find("Romaine Base")->max_dispense_weight_g, 100, 0.01);
    expect_eq("Farmstand Mexican Rice source",
              to_string(farmstand->find("Mexican Rice")->source), "template");
    expect_near("Farmstand Mexican Rice portion",
                farmstand->find("Mexican Rice")->full_portion(), 136, 0.01);
    expect_near("Cowgirl Mexican Rice portion",
                cowgirl->find("Mexican Rice")->full_portion(), 180, 0.01);

    std::printf("\nFloor matching (first rule wins, must_not_have vetoes)\n");
    expect_near("Farmstand romaine only",
                farmstand->floor_for({"Romaine Base"})->minimum_product_weight_g, 365, 0.01);
    expect_near("Farmstand kale only",
                farmstand->floor_for({"Massaged Kale"})->minimum_product_weight_g, 300, 0.01);
    expect_near("Farmstand grain only",
                farmstand->floor_for({"White Rice"})->minimum_product_weight_g, 455, 0.01);
    expect_near("Farmstand green + grain",
                farmstand->floor_for({"Romaine Base", "White Rice"})->minimum_product_weight_g,
                400, 0.01);
    expect_near("Farmstand two greens (ExactlyOne fails)",
                farmstand->floor_for({"Romaine Base", "Massaged Kale"})
                    ->minimum_product_weight_g,
                400, 0.01);
    ++checks;
    if (meatrice->floor_for({"White Rice"}) != nullptr) {
        ++failures;
        std::printf("  FAIL Meat + Rice should match no floor\n");
    } else {
        std::printf("  ok   Meat + Rice matches no floor\n");
    }

    std::printf("\nEnd-to-end recipes (against the HTML simulator's output)\n");
    struct { const Menu *menu; const char *recipe; double g, oz; Verdict v; } cases[] = {
        {cowgirl,   "The Hungry Cowgirl Bowl", 578, 28.5, Verdict::FitsMassBound},
        {cowgirl,   "Pollo Verde Asado Bowl",  125,  4.4, Verdict::Saturated},
        {siren,     "The Hungry Siren Bowl",   571, 26.5, Verdict::FitsMassBound},
        {farmstand, "Farmstand Ranch Salad",   407, 41.6, Verdict::OverAtStart},
        {pita,      "The Oasis",               620, 27.9, Verdict::FitsMassBound},
        {meatrice,  "Meat Rice Meat",          545, 27.5, Verdict::NoFloor},
    };
    for (const auto &c : cases) {
        SimResult r = run_recipe(*c.menu, c.recipe, curves);
        if (r.frames.empty()) continue;
        std::printf("  -- %s / %s\n", qPrintable(c.menu->brand), c.recipe);
        expect_near(QString("   final oz"), r.last().ounces, c.oz, 0.15);
        expect_eq(QString("   verdict"), verdict_name(r.verdict), verdict_name(c.v));
    }

    std::printf("\nBuild-your-own default (Farmstand, romaine + first protein)\n");
    {
        std::vector<BowlItem> items;
        items.push_back(make_item(*farmstand->find("Romaine Base"), curves, Method::Robot,
                                  3.5, 4.5));
        items.push_back(make_item(*farmstand->find("Chicken Herby"), curves, Method::Robot,
                                  3.5, 4.5));
        SimSettings s;
        s.floor_g = farmstand->floor_for(names_in(items))->minimum_product_weight_g;
        SimResult r = simulate(items, s);
        expect_near("floor", s.floor_g, 365, 0.01);
        expect_near("final g", r.last().grams, 225, 0.6);
        expect_near("final oz", r.last().ounces, 27.8, 0.15);
        expect_eq("verdict", verdict_name(r.verdict), "Saturated");
    }

    std::printf("\nCompression model (reduces to the plain fit at zero load)\n");
    {
        const Fit *f = curves.fit("Massaged Kale", Method::Robot);
        BowlItem kale;
        kale.name = "Massaged Kale";
        kale.kind = Kind::Base;
        kale.has_curve = true;
        kale.K = f->K;
        kale.p = f->p;
        kale.start_g = 110;
        kale.max_g = 110;
        SimSettings s;
        s.floor_g = 0;
        s.compress = true;
        SimResult alone = simulate({kale}, s);
        expect_near("kale 110 g, nothing on top", alone.last().ounces,
                    f->volume_oz(110), 0.001);

        BowlItem topping;
        topping.name = "Load";
        topping.kind = Kind::Topping;
        topping.start_g = 150;
        topping.max_g = 150;
        topping.flat_oz_per_100g = 0;
        SimResult loaded = simulate({kale, topping}, s);
        expect_near("kale 110 g under 150 g", loaded.last().per_item_oz[0], 22.30, 0.02);
    }

    std::printf("\nCSV ingest\n");
    {
        CurveSet c2;
        QString e;
        c2.load_csv(assets + "/data/mass_to_volume.csv", &e);
        expect_near("built-in rows", c2.observations().size(), 36, 0.01);
        expect_near("built-in ingredients", c2.ingredients().size(), 3, 0.01);

        // A CSV that adds points to a known ingredient and introduces two new ones,
        // using the alternative column spellings and a missing method column.
        const double kale_p_before = c2.fit("Massaged Kale", Method::Robot)->p;
        int added = 0;
        QStringList fresh;
        QString note;
        const bool ok = c2.load_csv(assets + "/tests/extra_curve.csv", &note, &added, &fresh);
        ++checks;
        if (!ok) { ++failures; std::printf("  FAIL upload rejected: %s\n", qPrintable(note)); }
        else std::printf("  ok   upload accepted\n");
        expect_near("rows added", added, 7, 0.01);
        expect_near("ingredients after", c2.ingredients().size(), 5, 0.01);
        expect_eq("new curves reported", fresh.join(", "), "Quinoa, Sweet Potato");

        // "kale" and "KALE " must land in the existing bucket, not create new ones.
        expect_near("kale robot n after", c2.count("Massaged Kale", Method::Robot), 8, 0.01);
        ++checks;
        if (std::fabs(c2.fit("Massaged Kale", Method::Robot)->p - kale_p_before) < 1e-9) {
            ++failures;
            std::printf("  FAIL kale curve did not refit after new points\n");
        } else {
            std::printf("  ok   kale curve refitted\n");
        }

        // A brand-new ingredient with four points gets a usable curve.
        const Fit *sweet = c2.fit("Sweet Potato", Method::Robot);
        ++checks;
        if (!sweet || !sweet->valid) { ++failures; std::printf("  FAIL no Sweet Potato fit\n"); }
        else std::printf("  ok   Sweet Potato fitted: K=%.3f p=%.3f R2=%.3f\n",
                         sweet->K, sweet->p, sweet->r2);
        if (sweet) expect_near("Sweet Potato in measured range", sweet->volume_oz(150), 10.4, 0.4);

        // One point is not a curve.
        ++checks;
        if (c2.fit("Quinoa", Method::Pooled)) {
            ++failures;
            std::printf("  FAIL a single point produced a curve\n");
        } else {
            std::printf("  ok   single-point ingredient produces no curve\n");
        }

        // Reset must discard uploads and restore exactly the bundled set.
        c2.reset_to_builtin(assets + "/data/mass_to_volume.csv");
        expect_near("rows after reset", c2.observations().size(), 36, 0.01);
        expect_near("ingredients after reset", c2.ingredients().size(), 3, 0.01);

        // A metric sheet is the same measurement in another unit: it must land on the
        // same curve as an ounces sheet would, not a curve 29.6x larger.
        CurveSet mc;
        QString mnote;
        int madded = 0;
        ++checks;
        if (!mc.load_csv(assets + "/tests/metric.csv", &mnote, &madded)) {
            ++failures;
            std::printf("  FAIL metric CSV rejected: %s\n", qPrintable(mnote));
        } else {
            std::printf("  ok   metric CSV accepted\n");
        }
        expect_near("metric rows", madded, 2, 0.01);
        // 710 ml is 24.0 fl oz; the stored observation must be in fluid ounces.
        ++checks;
        if (mc.observations().empty()) {
            ++failures;
            std::printf("  FAIL no metric observations\n");
        } else {
            const double got = mc.observations().front().ounces;
            if (std::fabs(got - 710.0 / 29.5735295625) > 0.01) {
                ++failures;
                std::printf("  FAIL metric not converted: %.3f\n", got);
            } else {
                std::printf("  ok   710 ml stored as %.2f fl oz\n", got);
            }
        }

        // A file missing the required columns is rejected with a useful message.
        CurveSet c3;
        QString why;
        ++checks;
        if (c3.load_csv(assets + "/tests/bad_columns.csv", &why)) {
            ++failures;
            std::printf("  FAIL bad-column CSV was accepted\n");
        } else if (!why.contains("g") || !why.contains("oz")) {
            ++failures;
            std::printf("  FAIL unhelpful error: %s\n", qPrintable(why));
        } else {
            std::printf("  ok   bad columns rejected: %s\n",
                        qPrintable(why.split('\n').first()));
        }
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
