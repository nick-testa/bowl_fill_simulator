///
/// Checks the C++ core against values produced by the HTML simulator it was ported
/// from. Any drift here means the port changed behaviour, which is the one thing
/// this rewrite must not do.
///
#include "core/adaptive.hh"
#include "core/bowl_data.hh"
#include "core/measurements.hh"
#include "core/curves.hh"
#include "core/feasibility.hh"
#include "core/menu_model.hh"
#include "core/simulator.hh"
#include "core/sweep.hh"

#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>
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

void expect_true(const QString &what, bool ok)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL %s\n", qPrintable(what)); }
    else std::printf("  ok   %s\n", qPrintable(what));
}

/// Number of fields in one RFC4180 record, so the report's quoting is checked rather
/// than assumed -- every summary sentence contains commas.
int csv_fields(const QString &line)
{
    int n = 1;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) ++n;
    }
    return n;
}

const RecipeAudit *row_named(const BrandAudit &a, const QString &name)
{
    for (const RecipeAudit &r : a.rows)
        if (r.recipe == name) return &r;
    return nullptr;
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
    // The published fits, and everything checked against the HTML simulator below,
    // were made on log-log; the app's default fits on the volume itself.
    curves.set_fit_space(FitSpace::Log);
    QString err;
    if (!curves.load_csv(assets + "/tests/data/mass_to_volume.csv", &err)) {
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
    for (const QString &w : loaded.issues) std::printf("  issue %s\n", qPrintable(w));
    expect_near("brands parsed", loaded.menus.size(), 7, 0.01);

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
    // Its ten cup sauces (Ranch, Tzatziki, the cremas...) are menu.sauces, not ingredients.
    expect_near("Farmstand ingredients", farmstand->ingredients.size(), 19, 0.01);
    expect_near("Farmstand sauces", farmstand->sauces.size(), 10, 0.01);
    expect_true("Farmstand Ranch is a sauce, not a topping",
                farmstand->is_sauce("Ranch") && !farmstand->find("Ranch"));
    // 10 plated templates plus the two build-your-own shells, which pin nothing but
    // still have to appear in the feasibility report.
    expect_near("Farmstand recipes", farmstand->recipes.size(), 12, 0.01);
    expect_near("Meat + Rice floors", meatrice->floors.size(), 0, 0.01);
    expect_near("Meat + Rice recipes", meatrice->recipes.size(), 4, 0.01);

    // Casa Condesa has no dynamic_portion_increases and none of the old ingredient
    // names: its options come from mappings.ingredients alone, kinds read from the
    // names, and sauces left to the sauce cups.
    if (const Menu *casa = by_brand(loaded.menus, "Casa Condesa")) {
        expect_near("Casa Condesa bases", casa->names_of(Kind::Base).size(), 2, 0.01);
        // Braised Beef is a stale template target borrowed from another brand.
        expect_near("Casa Condesa proteins", casa->names_of(Kind::Protein).size(), 4, 0.01);
        // Cotija goes in a cup though nothing in its name says so; the brand's own
        // spellings ("Green Salsa Macha") still land on the list.
        expect_near("Casa Condesa toppings", casa->names_of(Kind::Topping).size(), 2, 0.01);
        expect_eq("Casa Condesa sauces", casa->sauces.join(" | "),
                  "Cilantro Crema | Cotija | Green Salsa Macha | Guacachile Salsa | "
                  "Salsa Cremosa");
        expect_eq("Casa Condesa bases named",
                  casa->names_of(Kind::Base).join(" | "), "Mexican Rice | White Jasmine Rice");
        const Ingredient *rice = casa->find("Mexican Rice");
        expect_true("Casa Condesa rice is a base", rice && rice->kind == Kind::Base);
        expect_near("Casa Condesa rice portion", rice ? rice->full_portion() : 0, 160, 0.01);
        expect_true("Casa Condesa sauces are not toppings",
                    !casa->find("Salsa Cremosa Portioned"));
        expect_true("Casa Condesa sweeps", combinations_for(*casa, SweepLimits{}) > 0);
    } else {
        expect_true("Casa Condesa loaded", false);
    }

    // M+R2 spells White Jasmine Rice three ways across stores; they are one option.
    if (const Menu *mr2 = by_brand(loaded.menus, "M+R2")) {
        expect_eq("M+R2 bases named", mr2->names_of(Kind::Base).join(" | "),
                  "Mexican Rice | White Jasmine Rice");
        expect_true("M+R2 carnitas spellings merged", !mr2->find("Pork Carnitas Cooked v11"));
    } else {
        expect_true("M+R2 loaded", false);
    }

    std::printf("\nName mappings\n");
    {
        // Two brands spell the same chicken differently from a third; mapping both
        // onto the established name is what lets them share its cost and curve.
        NameMappings maps;
        maps[{"Casa Condesa", "Guajillo / Cumin Chicken, [cooked]"}] = {"Chicken Tex-Mex", ""};
        maps[{"M+R2", "Guajillo  Cumin Chicken cooked"}] = {"Chicken Tex-Mex", ""};
        maps[{"Casa Condesa", "Cotija portion"}] = {"", "sauce"};
        const LoadResult mapped = load_menus(assets + "/menus", &maps);
        const Menu *casa = by_brand(mapped.menus, "Casa Condesa");
        const Menu *mr2 = by_brand(mapped.menus, "M+R2");
        const Ingredient *tex = casa ? casa->find("Chicken Tex-Mex") : nullptr;
        expect_true("mapped name replaces the menu's", tex && !casa->find("Guajillo Cumin Chicken"));
        expect_true("mapped ingredient keeps its kind", tex && tex->kind == Kind::Protein);
        expect_near("mapped ingredient keeps its weight", tex ? tex->full_portion() : 0, 60, 0.01);
        expect_true("mapping applies per brand",
                    mr2 && mr2->find("Chicken Tex-Mex") && !mr2->find("Guajillo Cumin Chicken"));
        expect_true("kind override to sauce leaves the pool", casa && !casa->find("Cotija"));

        QString smoky_item;
        bool listed = false;
        if (casa) {
            for (const Recipe &r : casa->recipes)
                if (r.name == "Smoky Roasted Chicken Bowl" && !r.items.empty())
                    smoky_item = r.items[0].name;
            for (const MenuName &n : casa->names)
                if (n.raw == "Guajillo / Cumin Chicken, [cooked]")
                    listed = n.automatic == "Guajillo Cumin Chicken" && n.name == "Chicken Tex-Mex";
        }
        expect_eq("recipe target follows the mapping", smoky_item, "Chicken Tex-Mex");
        expect_true("menu lists raw, automatic and mapped names", listed);

        const QString path = QDir::temp().filePath("bowlfill-verify-aliases.csv");
        QString err;
        NameMappings back;
        const bool saved = save_name_mappings(path, maps, &err);
        const bool read = load_name_mappings(path, back, &err);
        expect_true("mappings round-trip through CSV (names with commas)",
                    saved && read && back == maps);
        QFile::remove(path);
    }

    {
        int built = 0;
        for (const Recipe &r : farmstand->recipes)
            if (r.customer_built) { ++built; if (!r.items.empty()) built = -99; }
        expect_near("Farmstand customer-built shells", built, 2, 0.01);
    }

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

    // Originally asserted against the HTML simulator. Recipes with sauces now differ
    // from it: the HTML dispenses a recipe's sauces into the bowl as toppings, which
    // double-counts them against the cups. Cowgirl, Siren, Ranch Salad and Meat Rice
    // Meat moved for that reason; the rest still match the HTML.
    std::printf("\nEnd-to-end recipes (sauces in cups, not in the bowl)\n");
    struct { const Menu *menu; const char *recipe; double g, oz; Verdict v; } cases[] = {
        {cowgirl,   "The Hungry Cowgirl Bowl", 578, 24.1, Verdict::FitsMassBound},
        {cowgirl,   "Pollo Verde Asado Bowl",  125,  4.4, Verdict::Saturated},
        {siren,     "The Hungry Siren Bowl",   571, 24.8, Verdict::FitsMassBound},
        {farmstand, "Farmstand Ranch Salad",   407, 50.9, Verdict::OverAtStart},
        {pita,      "The Oasis",               620, 27.9, Verdict::FitsMassBound},
        {meatrice,  "Meat Rice Meat",          545, 23.1, Verdict::NoFloor},
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
        c2.load_csv(assets + "/tests/data/mass_to_volume.csv", &e);
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
        c2.reset_to_builtin(assets + "/tests/data/mass_to_volume.csv");
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

    std::printf("\nAdaptive Dispense solver\n");
    {
        CostTable costs;
        QString cerr;
        ++checks;
        if (!costs.load_csv(assets + "/data/ingredient_costs.csv", &cerr)) {
            ++failures;
            std::printf("  FAIL cost table: %s\n", qPrintable(cerr));
        } else {
            std::printf("  ok   cost table loaded, %d priced ingredients\n", costs.size());
        }
        // 100 g of romaine at 2.60/kg.
        bool known = false;
        expect_near("cost of 100 g romaine",
                    costs.cost_of("Romaine Base", Kind::Base, 100, &known), 0.26, 0.001);
        ++checks;
        if (!known) { ++failures; std::printf("  FAIL romaine should be a real entry\n"); }
        else std::printf("  ok   romaine priced from the table\n");
        bool unknown_known = true;
        costs.cost_of("Nonesuch", Kind::Protein, 100, &unknown_known);
        ++checks;
        if (unknown_known) { ++failures; std::printf("  FAIL unlisted should fall back\n"); }
        else std::printf("  ok   unlisted ingredient falls back and is flagged\n");

        // The tolerance table and the "4:1:2 ratio steps" are one requirement.
        Tolerances t;
        expect_near("base:protein ratio", t.base / t.protein, 4.0, 1e-9);
        expect_near("topping:protein ratio", t.topping / t.protein, 2.0, 1e-9);

        AdaptiveSettings as;
        as.bowl_capacity_oz = 32.0;
        as.sauce.cups = 2;
        expect_near("two sauce cups in oz", as.sauce_volume_oz(), 3.381, 0.01);

        auto recipe_items = [&](const Menu &m, const char *name) {
            auto it = std::find_if(m.recipes.begin(), m.recipes.end(),
                                   [&](const Recipe &r) { return r.name == name; });
            return bowl_from_recipe(m, *it, curves, Method::Robot);
        };

        // Scenario 2: a bowl a little over the band is pulled back onto target. The
        // Oasis is 27.9 oz nominal against a 27.2 oz target. (The Cowgirl Bowl used to
        // be the example, but without its sauces in the bowl it sits inside the band.)
        std::vector<BowlItem> cow = recipe_items(*pita, "The Oasis");
        AdaptiveResult ar = solve_adaptive(cow, as, &costs);
        expect_eq("oasis status", to_string(ar.status), "adjusted");
        expect_near("oasis lands on target", ar.food_volume_oz,
                    as.target_fill * 32.0, 0.05);
        ++checks;
        if (ar.alpha >= 0) { ++failures; std::printf("  FAIL expected a reduction\n"); }
        else std::printf("  ok   alpha negative (%.3f), a reduction\n", ar.alpha);

        // Protein must move least and base most -- the point of the tolerance table.
        double base_move = 0, protein_move = 0;
        for (const AdaptiveItem &i : ar.items) {
            if (i.kind == Kind::Base) base_move = std::fabs(i.delta_pct());
            if (i.kind == Kind::Protein) protein_move = std::fabs(i.delta_pct());
        }
        ++checks;
        if (protein_move > 5.001 || base_move > 20.001) {
            ++failures;
            std::printf("  FAIL tolerance exceeded: base %.1f%%, protein %.1f%%\n",
                        base_move, protein_move);
        } else {
            std::printf("  ok   within tolerance: base %.1f%%, protein %.1f%%\n",
                        base_move, protein_move);
        }
        ++checks;
        if (base_move <= protein_move) {
            ++failures;
            std::printf("  FAIL base should absorb more than protein\n");
        } else {
            std::printf("  ok   base absorbs %.1fx what protein does\n",
                        base_move / protein_move);
        }

        // Scenario 1: a bowl already inside the visual band is left alone. The Ranch
        // Salad's nominal is 34.5 oz, so a 43 oz bowl puts it at ~80%.
        std::vector<BowlItem> ranch = recipe_items(*farmstand, "Farmstand Ranch Salad");
        AdaptiveSettings wide = as;
        wide.bowl_capacity_oz = 43.0;
        AdaptiveResult none = solve_adaptive(ranch, wide, &costs);
        expect_eq("wide bowl status", to_string(none.status), "no adjustment");
        expect_near("wide bowl alpha", none.alpha, 0.0, 1e-9);
        expect_near("wide bowl is untouched", none.food_volume_oz,
                    none.nominal_volume_oz, 1e-9);

        // Scenario 3: the tolerance bands are not wide enough to save this bowl.
        // 34.5 oz nominal against a 27.2 oz target is a 21% cut; base can give 20%,
        // protein 5%. The honest answer is a flagged underfill, not a fit.
        AdaptiveResult hard = solve_adaptive(ranch, as, &costs);
        expect_eq("ranch salad at 32 oz", to_string(hard.status),
                  "still over at max reduction");
        expect_near("ranch alpha pinned", hard.alpha, -1.0, 1e-9);
        ++checks;
        if (hard.food_volume_oz <= as.target_fill * 32.0) {
            ++failures;
            std::printf("  FAIL expected it to remain over target\n");
        } else {
            std::printf("  ok   best effort %.1f oz vs %.1f oz target -- flagged\n",
                        hard.food_volume_oz, as.target_fill * 32.0);
        }

        // Raising the target must never lower the volume delivered.
        double prev = -1;
        bool monotone = true;
        for (double t = 0.30; t <= 1.2001; t += 0.05) {
            AdaptiveSettings s2 = as;
            s2.band_low = 2.0;          // disable the dead band for this sweep
            s2.band_high = 2.0;
            s2.target_fill = t;
            const double v = solve_adaptive(cow, s2, &costs).food_volume_oz;
            if (v + 1e-9 < prev) monotone = false;
            prev = v;
        }
        ++checks;
        if (!monotone) { ++failures; std::printf("  FAIL volume not monotone in target\n"); }
        else std::printf("  ok   delivered volume monotone in target\n");

        // COGS is reported, never solved for.
        AdaptiveSettings priced = as;
        priced.menu_price = 12.95;
        AdaptiveResult pr = solve_adaptive(cow, priced, &costs);
        ++checks;
        if (pr.total_cost <= 0) { ++failures; std::printf("  FAIL no cost computed\n"); }
        else std::printf("  ok   COGS %.1f%% of $%.2f (guardrail %.0f%%) -- %s\n",
                         100 * pr.cogs_ratio(), priced.menu_price,
                         100 * priced.cogs_target,
                         pr.cogs_within_guardrail() ? "within" : "OVER");
        expect_near("solving for volume ignores price",
                    pr.food_volume_oz, ar.food_volume_oz, 1e-9);
    }

    std::printf("\nPer-brand feasibility audit\n");
    {
        SimSettings ls;
        AdaptiveSettings as;
        CostTable costs;
        costs.load_csv(assets + "/data/ingredient_costs.csv");
        const BrandAudit a =
            audit_brand(*farmstand, curves, Method::Robot, ls, as, &costs);

        expect_true("every recipe is audited",
                    a.rows.size() == farmstand->recipes.size());
        expect_true("counts partition the rows",
                    a.complete + a.partial + a.customer_built
                        == static_cast<int>(a.rows.size()));

        bool ordered = true;
        for (size_t i = 1; i < a.rows.size(); ++i)
            if (a.rows[i].severity() > a.rows[i - 1].severity()) ordered = false;
        expect_true("rows are ordered worst first", ordered);

        int failing = 0, customer_built = 0;
        for (const RecipeAudit &r : a.rows) {
            if (r.customer_built) { ++customer_built; continue; }
            if (!r.legacy_ok() && r.complete_bowl) ++failing;
        }
        expect_true("legacy_failing counts exactly the failing recipes",
                    a.legacy_failing() == failing);
        expect_true("overflowing and never-filling are counted apart",
                    a.legacy_over > 0 && a.legacy_short > 0);
        expect_true("customer-built bowls are excluded from the failure count",
                    a.customer_built == customer_built && customer_built > 0);

        const RecipeAudit *ranch = row_named(a, "Farmstand Ranch Salad");
        expect_true("Ranch Salad is found", ranch != nullptr);
        if (ranch) {
            expect_true("Ranch Salad overflows as written",
                        ranch->legacy.verdict == Verdict::OverAtStart);
            expect_true("Ranch Salad is judged a complete bowl", ranch->complete_bowl);
            expect_true("tolerances cannot rescue Ranch Salad",
                        ranch->adaptive.status == AdaptiveStatus::StillOver);
        }

        const RecipeAudit *chili = row_named(a, "Chili Crisp Crunch Salad");
        expect_true("Chili Crisp never reaches its floor",
                    chili && chili->legacy.verdict == Verdict::Saturated);

        const RecipeAudit *byo = row_named(a, "Build Your Own Farmstand Bowl");
        expect_true("the build-your-own template is audited, not dropped",
                    byo && byo->customer_built && byo->pinned == 0);
        expect_true("a customer-built bowl is never reported as a failure",
                    byo && byo->severity() == 0);

        // A partial template pins only a protein; judging it as a finished bowl would
        // report a false failure, so it is separated rather than counted as one.
        const BrandAudit cow =
            audit_brand(*cowgirl, curves, Method::Robot, ls, as, &costs);
        const RecipeAudit *pollo = row_named(cow, "Pollo Verde Asado Bowl");
        expect_true("a one-ingredient template is flagged partial, not complete",
                    pollo && !pollo->complete_bowl && !pollo->customer_built);
        expect_true("a partial falling short is not a failure",
                    pollo && pollo->legacy.verdict == Verdict::Saturated
                        && pollo->severity() == 0);
        expect_true("finished bowls sort above modifier templates at equal severity",
                    cow.rows.front().complete_bowl);
        // Seven protein-only templates all fall short; none of them may be counted.
        // The one shortfall that remains is a finished bowl, Hungry Cowboy.
        expect_true("partial shortfalls stay out of the brand's failure count",
                    cow.partial == 7 && cow.legacy_short == 0 && cow.adaptive_short == 1);

        const QStringList lines = a.to_csv().split('\n', Qt::SkipEmptyParts);
        expect_true("CSV carries a header and one row per recipe",
                    lines.size() == static_cast<int>(a.rows.size()) + 1);
        const int width = csv_fields(lines.first());
        bool rectangular = true;
        for (const QString &line : lines)
            if (csv_fields(line) != width) rectangular = false;
        expect_true("CSV rows are rectangular once quoting is honoured", rectangular);
    }

    std::printf("\nSauce cups take space in both models\n");
    {
        SimSettings none;
        none.sauce.cups = 0;
        expect_near("no cups leaves the whole bowl", none.food_capacity_oz(), 32.0, 1e-9);
        SimSettings two;
        two.sauce.cups = 2;
        expect_near("two cups", two.food_capacity_oz(), 32.0 - 3.381, 0.01);
        SimSettings over_max;
        over_max.sauce.cups = 5;
        expect_near("cups are capped at two", over_max.food_capacity_oz(),
                    two.food_capacity_oz(), 1e-9);

        // Yiayia's Garden fills the bowl almost exactly, so it is the case that the cups
        // decide. (Old Hank used to be; without Seeds in the bowl it ramps over anyway.)
        const Recipe *hank = nullptr;
        for (const Recipe &r : farmstand->recipes)
            if (r.name == "Yiayia S Mediterranean Garden") hank = &r;
        expect_true("Yiayia's Garden is found", hank != nullptr);
        if (hank) {
            std::vector<BowlItem> items =
                bowl_from_recipe(*farmstand, *hank, curves, Method::Robot);
            SimSettings s;
            s.floor_g = farmstand->floor_for(names_in(items))->minimum_product_weight_g;
            s.sauce.cups = 0;
            expect_eq("without cups it fits", verdict_name(simulate(items, s).verdict),
                      "Fits");
            // It starts under the reduced ceiling and the ramp pushes it through.
            s.sauce.cups = 1;
            expect_eq("one cup puts it over", verdict_name(simulate(items, s).verdict),
                      "OverWhileRamping");
        }
    }

    std::printf("\nBowl geometry: cups and chunks by height\n");
    {
        // 32 oz at a 44.5 mm rim; cups 35 mm tall and 62 mm across.
        BowlGeometry g;
        g.enabled = true;
        SauceCups none, two;
        none.cups = 0;
        two.cups = 2;
        const double kOz = 29.5735295625;
        const double cup_column_oz = 2 * M_PI * 31.0 * 31.0 * 35.0 / 1000.0 / kOz;

        expect_near("no cups, no chunks: the whole bowl", food_capacity_oz(32, none, g, 0),
                    32.0, 1e-9);
        expect_near("pressed cups cost their columns", food_capacity_oz(32, two, g, 0),
                    32.0 - cup_column_oz, 1e-6);
        BowlGeometry resting = g;
        resting.cups_pressed = false;
        expect_near("resting cups hold the whole surface a cup below the lid",
                    food_capacity_oz(32, two, resting, 0), 32.0 * (44.5 - 35.0) / 44.5, 1e-6);

        // A 20 mm protein standing half proud needs 10 mm everywhere, and the cup
        // columns shrink by the same 10 mm because the surface under them is already
        // that much lower.
        const double chunky = 32.0 * (44.5 - 10.0) / 44.5
                              - 2 * M_PI * 31.0 * 31.0 * 25.0 / 1000.0 / kOz;
        expect_near("chunks and pressed cups", food_capacity_oz(32, two, g, 20), chunky, 1e-6);

        BowlGeometry domed = g;
        domed.lid_headroom_mm = 10;
        expect_near("a 10 mm dome adds half its cylinder",
                    food_capacity_oz(32, none, domed, 0), 32.0 * (44.5 + 5.0) / 44.5, 1e-6);

        BowlGeometry off;
        expect_near("geometry off keeps the volume model", food_capacity_oz(32, two, off, 20),
                    32.0 - two.volume_oz(), 1e-9);

        BowlItem rice, chicken;
        rice.kind = Kind::Base;
        rice.start_g = 150;
        chicken.kind = Kind::Protein;
        chicken.start_g = 95;
        chicken.piece_height_mm = 20;
        SimSettings ss;
        ss.geometry = g;
        const SimResult sr = simulate({rice, chicken}, ss);
        expect_near("simulate() finds the tallest chunk", sr.settings.chunk_height_mm, 20, 1e-9);
        expect_near("and judges the bowl against it", sr.settings.food_capacity_oz(),
                    food_capacity_oz(32, ss.sauce, g, 20), 1e-9);
        AdaptiveSettings as;
        as.geometry = g;
        const AdaptiveResult ar = solve_adaptive({rice, chicken}, as, nullptr);
        expect_near("adaptive charges the same overhead", ar.settings.sauce_volume_oz(),
                    32.0 - food_capacity_oz(32, as.sauce, g, 20), 1e-9);
    }

    std::printf("\nPiece heights\n");
    {
        PieceHeights pieces{{"Chicken Tex-Mex", 25.0}, {"Beef Shawarma", 0.0}};
        const LoadResult withp = load_menus(assets + "/menus", nullptr, &pieces);
        const Menu *cg = by_brand(withp.menus, "The Hungry Cowgirl");
        const Ingredient *tex = cg ? cg->find("Chicken Tex-Mex") : nullptr;
        const Ingredient *shaw = cg ? cg->find("Beef Shawarma") : nullptr;
        const Ingredient *rice = cg ? cg->find("Mexican Rice") : nullptr;
        expect_near("a listed height is used", tex ? tex->piece_height_mm : -1, 25, 1e-9);
        expect_near("a listed 0 smears", shaw ? shaw->piece_height_mm : -1, 0, 1e-9);
        expect_true("an unlisted base smears", rice && rice->piece_height_mm == 0);
        const Ingredient *herby = nullptr;
        for (const Menu &m : withp.menus)
            if (!herby) herby = m.find("Chicken Herby");
        expect_true("an unlisted protein takes the placeholder",
                    herby && herby->piece_height_mm == kPlaceholderProteinPieceMm
                        && herby->piece_height_placeholder);

        const QString path = QDir::temp().filePath("bowlfill-verify-pieces.csv");
        PieceHeights back;
        expect_true("piece heights round-trip through CSV",
                    save_piece_heights(path, pieces) && load_piece_heights(path, back)
                        && back == pieces);
        QFile::remove(path);
    }

    std::printf("\nCombinatorial sweep\n");
    {
        // Bases only, one sauce: 5 single-base bowls plus 10 pairs.
        SweepLimits base_only;
        base_only.min_proteins = base_only.max_proteins = 0;
        base_only.min_toppings = base_only.max_toppings = 0;
        base_only.max_sauces = 1;
        expect_near("base-only combinations", combinations_for(*farmstand, base_only), 15,
                    0.01);

        SweepLimits lim = base_only;
        lim.min_proteins = lim.max_proteins = 1;
        expect_near("exactly one protein", combinations_for(*farmstand, lim), 15 * 3, 0.01);
        lim.min_toppings = lim.max_toppings = 1;
        // 11 toppings, now that the sauces go in cups instead.
        expect_near("exactly one topping", combinations_for(*farmstand, lim), 15 * 3 * 11,
                    0.01);
        lim.max_sauces = 2;
        expect_near("the second sauce doubles the space", combinations_for(*farmstand, lim),
                    15 * 3 * 11 * 2, 0.01);

        // The menu requires a base, a topping and a sauce, so the default sweep must
        // never emit a bowl missing any of them.
        const SweepLimits fallback;
        SweepLimits no_floor_bounds = fallback;
        no_floor_bounds.min_toppings = 0;
        expect_true("dropping the topping minimum admits strictly more bowls",
                    combinations_for(*farmstand, no_floor_bounds)
                        > combinations_for(*farmstand, fallback));
        SweepLimits no_protein_bound = fallback;
        no_protein_bound.min_proteins = 0;
        expect_true("dropping the protein minimum admits strictly more bowls",
                    combinations_for(*farmstand, no_protein_bound)
                        > combinations_for(*farmstand, fallback));

        bool grows = true;
        qint64 prev = 0;
        for (int k = 1; k <= 5; ++k) {
            SweepLimits step;
            step.max_toppings = k;
            const qint64 n = combinations_for(*farmstand, step);
            if (n <= prev) grows = false;
            prev = n;
        }
        expect_true("raising the topping cap strictly grows the space", grows);

        const SweepPlan plan = plan_sweep(*farmstand, base_only);
        expect_near("plan rows = combinations x recipes", plan.rows(), 15 * 12, 0.01);

        QString csv;
        QTextStream out(&csv);
        std::vector<SweepTotals> totals;
        // Compression is forced on for every swept bowl, so passing it off must not
        // change what comes out.
        SimSettings no_compress;
        no_compress.compress = false;
        const bool finished =
            run_sweep({farmstand}, curves, Method::Robot, no_compress, AdaptiveSettings{},
                      nullptr, base_only, out, {}, &totals);
        out.flush();
        expect_true("sweep runs to completion", finished);
        expect_near("rows emitted = rows planned", totals.at(0).rows, plan.rows(), 0.01);

        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        expect_near("CSV line count", lines.size(), plan.rows() + 1, 0.01);
        const int width = csv_fields(lines.first());
        bool rectangular = true;
        for (const QString &line : lines)
            if (csv_fields(line) != width) rectangular = false;
        expect_true("sweep CSV is rectangular", rectangular);

        expect_near("verdicts partition the rows",
                    totals.at(0).legacy_fits + totals.at(0).legacy_over
                        + totals.at(0).legacy_short,
                    totals.at(0).rows, 0.01);

        // A recipe pin must survive into the swept bowl, and must not leak into a bowl
        // built by a recipe that does not pin that ingredient.
        const double romaine_portion = farmstand->find("Romaine Base")->full_portion();
        double pinned_g = 0, shell_g = 0;
        for (const QString &line : lines) {
            const QStringList f = line.split(',');
            if (f.size() < 9 || f[3] != "Romaine Base") continue;
            if (f[1] == "Farmstand Ranch Salad") pinned_g = f[8].toDouble();
            if (f[1] == "Build Your Own Farmstand Bowl") shell_g = f[8].toDouble();
        }
        expect_near("a customer-built shell uses the menu portion", shell_g,
                    romaine_portion, 0.51);
        expect_true("a recipe's pinned weight overrides the menu portion",
                    pinned_g > 0 && std::fabs(pinned_g - romaine_portion) > 0.5);

        // Every recipe has to appear, including the shells the dropdown hides.
        int distinct = 0;
        for (const Recipe &r : farmstand->recipes)
            if (csv.contains("," + r.name + ",")) ++distinct;
        expect_near("every recipe appears in the sweep", distinct,
                    farmstand->recipes.size(), 0.01);
    }


    std::printf("\nBowl data import (one row per scanned bowl)\n");
    {
        // Volumes generated from the model at a known squash, so the fit must recover it.
        const double truth = 3.0;
        auto row = [](std::vector<std::pair<QString, double>> items, int cups) {
            BowlRow r;
            r.items = std::move(items);
            r.cups = cups;
            return r;
        };
        const std::vector<BowlRow> mixed = {
            row({{"Romaine Base", 55}, {"White Jasmine Rice", 90}, {"Chicken Al Pastor", 100},
                 {"Pico de Gallo", 50}}, 2),
            row({{"Romaine Base", 100}, {"Pico de Gallo", 50}, {"Feta", 25}}, 0),
            row({{"Romaine Base", 80}, {"Massaged Kale", 40}, {"Chicken Al Pastor", 100}}, 1),
        };
        auto ml = [&](const BowlRow &r) {
            return predicted_scan_oz(r, curves, Method::Robot, truth) * CurveSet::kMlPerFlOz;
        };

        QTemporaryDir dir;
        const QString path = dir.path() + "/bowls.csv";
        QFile f(path);
        expect_true("temp file opens", f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        // Station spellings, in station (dispense) order.
        out << "Romaine Base,Massaged Kale,White Jasmine Rice retherm v11,"
               "Chicken Al Pastor [Cooked],Pico de Gallo,Feta,ml,cups,lid,notes\n";
        out << "55,,90,100,50,," << ml(mixed[0]) << ",2,closed,first bowl\n";
        out << "100,,,,50,25," << ml(mixed[1]) << ",,,\n";
        out << "80,40,,100,,," << ml(mixed[2]) << ",1,forced,\n";
        out << "60,,,,,,390,,,ladder\n";
        out << "120,,,,,,760,,,ladder\n";
        out << ",,,,,,,,,blank row with a note\n";
        f.close();

        BowlSheet sheet;
        QString err;
        expect_true("bowl sheet loads", load_bowl_csv(path, sheet, &err));
        expect_eq("station names are cleaned",
                  sheet.ingredients.join(" | "),
                  "Romaine Base | Massaged Kale | White Jasmine Rice | Chicken Al Pastor | "
                  "Pico de Gallo | Feta");
        expect_near("bowls read", sheet.rows.size(), 5, 0.01);
        expect_near("mixed bowls", sheet.mixed_rows(), 3, 0.01);
        expect_near("rows skipped", sheet.skipped, 1, 0.01);
        expect_eq("columns keep dispense order",
                  sheet.rows[0].items.front().first + " > " + sheet.rows[0].items.back().first,
                  "Romaine Base > Pico de Gallo");
        expect_near("cups read", sheet.rows[0].cups, 2, 0.01);
        expect_eq("lid read", sheet.rows[2].lid, "forced");

        const std::vector<Observation> pts = single_ingredient_points(sheet);
        expect_near("ladder rows become curve points", pts.size(), 2, 0.01);
        expect_near("ml converted to oz", pts.empty() ? 0 : pts[0].ounces,
                    390 / CurveSet::kMlPerFlOz, 1e-9);

        const SquashFit fit = fit_squash(sheet, curves, Method::Robot);
        expect_true("squash fit is valid", fit.valid);
        expect_near("squash recovers the generating factor", fit.load_transfer, truth, 0.01);
        expect_near("fitted error is ~0", fit.rmse_fitted_oz, 0.0, 0.01);
        expect_true("fitted beats no squash", fit.rmse_fitted_oz < fit.rmse_uncompressed_oz);
        expect_true("unmeasured ingredients are flagged",
                    fit.flat_rate.contains("Chicken Al Pastor")
                        && fit.flat_rate.contains("Pico de Gallo"));
    }

    std::printf("\nCurve fitting on the volume itself\n");
    {
        const double kOz = CurveSet::kMlPerFlOz;
        auto fit_of = [](const std::vector<std::pair<double, double>> &pts, FitSpace space) {
            CurveSet c;
            c.set_fit_space(space);
            for (const auto &[g, oz] : pts) c.add({"probe", Method::Robot, g, oz});
            c.refit();
            const Fit *f = c.fit("probe", Method::Robot);
            return f ? *f : Fit{};
        };
        // Exact power-law data is recovered exactly.
        std::vector<std::pair<double, double>> exact;
        for (double g = 20; g <= 400; g += 20) exact.push_back({g, 0.5 * std::pow(g, 0.8)});
        const Fit lin = fit_of(exact, FitSpace::Linear);
        expect_near("recovers K", lin.K, 0.5, 1e-4);
        expect_near("recovers p", lin.p, 0.8, 1e-4);

        // A camera reading 5 ml low on every scan bends a log-log fit far more than a
        // fit on the volume: the small readings carry the log fit.
        std::vector<std::pair<double, double>> low;
        for (double g = 10; g <= 450; g += 20)
            low.push_back({g, (1.1 * g - 5.0) / kOz});   // truly linear, 5 ml short
        const double p_log = fit_of(low, FitSpace::Log).p, p_lin = fit_of(low, FitSpace::Linear).p;
        expect_true(QString("a 5 ml zero error bends log-log (p %1) more than linear (p %2)")
                        .arg(p_log, 0, 'f', 3).arg(p_lin, 0, 'f', 3),
                    p_log > p_lin && p_lin < 1.03);

        // Compression must leave a curve with p >= 1 at its measured volume: it shows
        // no self-compaction to scale. It used to collapse to K*g, even unloaded.
        {
            BowlItem rice;
            rice.name = "white rice";
            rice.kind = Kind::Base;
            rice.has_curve = true;
            rice.K = 0.02;
            rice.p = 1.13;
            rice.start_g = 150;
            BowlItem chicken;
            chicken.kind = Kind::Protein;
            chicken.start_g = 120;
            SimSettings on;
            on.compress = true;
            on.load_transfer = 1.0;
            const double want = rice.volume_oz(150);
            expect_near("p > 1 base keeps its volume under load",
                        simulate({rice, chicken}, on).first().per_item_oz[0], want, 1e-9);
            expect_near("p > 1 base keeps its volume alone",
                        simulate({rice}, on).first().per_item_oz[0], want, 1e-9);
        }

        // The empty-bowl correction rescues small scans that read below zero.
        const QString path = QDir::temp().filePath("bowlfill-verify-zero.csv");
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write("test beans,ml\n2.3,-3\n4.4,-1\n100,105\n");
        }
        BowlSheet raw, fixed;
        load_bowl_csv(path, raw);
        load_bowl_csv(path, fixed, nullptr, -5.0);
        expect_near("uncorrected, negative readings are dropped", raw.rows.size(), 1, 0.01);
        expect_near("corrected by -5 ml, they count", fixed.rows.size(), 3, 0.01);
        expect_near("-3 ml reads as 2 ml", fixed.rows[0].ounces * kOz, 2.0, 1e-9);
        expect_near("105 ml reads as 110 ml", fixed.rows[2].ounces * kOz, 110.0, 1e-9);
        QFile::remove(path);
    }

    std::printf("\nComposite curves for unmeasured proteins and toppings\n");
    {
        CurveSet c;
        // Two measured proteins, linear for easy averaging: 1.0 and 2.0 ml per g.
        // Al Pastor has far more readings; each must still count once.
        const double kOz = CurveSet::kMlPerFlOz;
        for (double g = 50; g <= 200; g += 5)
            c.add({"Chicken Al Pastor", Method::Robot, g, 1.0 * g / kOz});
        for (double g : {50.0, 200.0}) c.add({"Suadero Beef", Method::Robot, g, 2.0 * g / kOz});
        for (double g : {50.0, 200.0}) c.add({"test beans", Method::Robot, g, 9.0 * g / kOz});
        for (double g : {50.0, 200.0}) c.add({"Mexican Slaw", Method::Robot, g, 3.0 * g / kOz});
        for (double g : {50.0, 200.0}) c.add({"white rice", Method::Robot, g, 1.5 * g / kOz});
        c.refit();

        QStringList members;
        const Fit pro = composite_curve(Kind::Protein, c, Method::Robot, &members);
        expect_eq("protein composite takes the measured proteins only",
                  members.join(", "), "Chicken Al Pastor, Suadero Beef");
        expect_near("each member counts once: halfway between 1.0 and 2.0 ml/g",
                    pro.volume_oz(120) * kOz, 1.5 * 120, 0.5);
        QStringList tops;
        composite_curve(Kind::Topping, c, Method::Robot, &tops);
        expect_eq("test curves are references, not food", tops.join(", "), "Mexican Slaw");
        expect_true("bases never get a composite",
                    !composite_curve(Kind::Base, c, Method::Robot).valid);

        const CurveChoice guajillo = choose_curve("Guajillo Cumin Chicken", Kind::Protein, c, Method::Robot);
        expect_true("an unmeasured protein uses the composite",
                    guajillo.source == CurveSource::Composite && guajillo.from.size() == 2);
        expect_true("a measured protein uses its own curve",
                    choose_curve("Suadero Beef", Kind::Protein, c, Method::Robot).source
                        == CurveSource::Own);
        expect_true("a family stand-in wins over the composite",
                    choose_curve("White Jasmine Rice", Kind::Base, c, Method::Robot).source
                        == CurveSource::Family);
        CurveSet empty;
        expect_true("nothing measured: flat rate",
                    choose_curve("Guajillo Cumin Chicken", Kind::Protein, empty, Method::Robot).source
                        == CurveSource::Flat);
    }

    std::printf("\nBackfill from a rigid reference, and impossible readings\n");
    {
        // A camera that reads true volume t as 0.8 * t^1.1 ml. Test beans are rigid
        // (true = g / 0.8); the food's true volume is 1.5 ml per g.
        auto camera = [](double true_ml) { return 0.8 * std::pow(true_ml, 1.1); };
        QDir root(QDir::temp().filePath("bowlfill-verify-backfill"));
        root.removeRecursively();
        QDir().mkpath(root.path());
        {
            QFile f(root.filePath("beans.csv"));
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            QTextStream out(&f);
            out << "test beans,ml\n";
            for (double g = 10; g <= 460; g += 15) out << g << "," << camera(g / 0.8) << "\n";
        }
        {
            QFile f(root.filePath("food.csv"));
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            QTextStream out(&f);
            out << "slaw,ml\n";
            for (double g : {20.0, 60.0, 120.0, 200.0}) out << g << "," << camera(1.5 * g) << "\n";
        }
        {
            QFile f(root.filePath("wet.csv"));
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write("pico,ml\n40,12\n60,70\n");   // 40 g reading 12 ml: below its solids
        }

        const double kOz = CurveSet::kMlPerFlOz;
        auto slaw_ratio = [&](const CurveSet &c, double g) {
            for (const Observation &o : c.observations())
                if (o.ingredient == "slaw" && std::fabs(o.grams - g) < 1e-6)
                    return o.ounces * kOz / (1.5 * g);
            return -1.0;
        };
        CurveSet plain, back;
        load_measurements(root.path(), plain);
        MeasurementOptions bo;
        bo.backfill = true;
        const MeasurementSet bs = load_measurements(root.path(), back, bo);
        expect_true("the reference is found", bs.backfill.valid && bs.backfill.reference == "test beans");
        expect_near("it learns the camera's bend", bs.backfill.p, 1.1, 0.01);
        const double low = slaw_ratio(back, 20), high = slaw_ratio(back, 200);
        expect_true(QString("without it, small readings run low (read/true %1 at 20 g, %2 at 200 g)")
                        .arg(slaw_ratio(plain, 20), 0, 'f', 2).arg(slaw_ratio(plain, 200), 0, 'f', 2),
                    slaw_ratio(plain, 20) < slaw_ratio(plain, 200) * 0.85);
        expect_near("backfilled, every size reads in the same proportion", low / high, 1.0, 0.01);
        CurveSet ref_check;
        bool beans_untouched = true;
        for (const Observation &o : back.observations())
            if (o.ingredient == "test beans")
                beans_untouched &= std::fabs(o.ounces * kOz - camera(o.grams / 0.8)) < 1e-3;
        expect_true("the reference itself is not corrected", beans_untouched);

        MeasurementOptions drop;
        drop.drop_impossible = true;
        CurveSet dropped_set;
        const MeasurementSet ds = load_measurements(root.path(), dropped_set, drop);
        expect_near("a reading below its solids is ignored", ds.dropped.count("pico") ? ds.dropped.at("pico") : 0, 1, 0.01);
        expect_near("the rest of that ingredient stays", dropped_set.count("pico", Method::Pooled), 1, 0.01);
        expect_near("off again, it is back", plain.count("pico", Method::Pooled), 2, 0.01);
        QFile wet(root.filePath("wet.csv"));
        wet.open(QIODevice::ReadOnly);
        expect_true("the file is not changed", wet.readAll().contains("40,12"));
        root.removeRecursively();
    }

    std::printf("\nMeasurement store (data/measurements)\n");
    {
        QDir root(QDir::temp().filePath("bowlfill-verify-store"));
        root.removeRecursively();
        QDir().mkpath(root.path());
        auto write = [&](const QString &name, const QString &text) {
            QFile f(root.filePath(name));
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write(text.toUtf8());
        };
        write("curves.csv", "method,ingredient,g,ml\nrobot,white rice,100,90\nrobot,White Rice,200,170\n");
        write("bowls.csv", "Romaine Base,Pico de Gallo,ml,notes\n60,,390,\n120,,760,\n55,50,500,mixed\n");
        write(".~lock.bowls.csv#", "junk");
        write("broken.csv", "not,a,measurement\n");

        CurveSet curves;
        const MeasurementSet set = load_measurements(root.path(), curves);
        expect_near("both formats load from one folder", curves.observations().size(), 4, 0.01);
        expect_near("lock files are ignored", set.files.size(), 3, 0.01);
        expect_near("a broken file is reported, not fatal", set.failed_files(), 1, 0.01);
        expect_near("mixed bowls are kept for the squash fit", set.bowls.mixed_rows(), 1, 0.01);
        expect_true("names match regardless of case",
                    curves.fit("WHITE RICE", Method::Robot) && curves.count("White rice", Method::Pooled) == 2);
        expect_eq("an unmeasured rice borrows a measured one",
                  proxy_curve_for("White Jasmine Rice", curves).toLower(), "white rice");

        CurveSet couscous;
        couscous.add({"lemon couscous", Method::Robot, 109, 98 / CurveSet::kMlPerFlOz});
        couscous.add({"lemon couscous", Method::Robot, 183, 213 / CurveSet::kMlPerFlOz});
        couscous.refit();
        expect_eq("plain couscous borrows lemon couscous", proxy_curve_for("Couscous", couscous),
                  "lemon couscous");
        expect_eq("a measured ingredient borrows nothing",
                  proxy_curve_for("Lemon Couscous", couscous), "");

        CurveSet reloaded;
        load_measurements(root.path(), reloaded);
        expect_near("loading twice does not double up", reloaded.observations().size(), 4, 0.01);

        // Uploads land in the folder; the same file twice is stored once.
        QDir outside(QDir::temp().filePath("bowlfill-verify-upload"));
        outside.removeRecursively();
        QDir().mkpath(outside.path());
        const QString up = outside.filePath("curves.csv");
        { QFile f(up); f.open(QIODevice::WriteOnly); f.write("ingredient,g,ml\nfeta,50,60\n"); }
        QString err;
        const QString stored = store_measurement_file(up, root.path(), &err);
        expect_eq("a name clash gets a numbered copy", QFileInfo(stored).fileName(), "curves_2.csv");
        expect_eq("the same file again is not copied twice",
                  QFileInfo(store_measurement_file(up, root.path(), &err)).fileName(), "curves_2.csv");
        CurveSet after;
        load_measurements(root.path(), after);
        expect_near("a stored upload loads with the rest", after.observations().size(), 5, 0.01);
        root.removeRecursively();
        outside.removeRecursively();
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
