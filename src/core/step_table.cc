#include "step_table.hh"

#include "text.hh"

#include <algorithm>
#include <cmath>
#include <format>

namespace bowlfill
{

namespace
{

constexpr double kMlPerOz = 29.5735295625; // US fluid ounce, as CurveSet::kMlPerFlOz

/// Unloaded volume of `item` at `grams`, in ml.
double ml_at(const BowlItem &item, double grams)
{
    return item.volume_oz(grams) * kMlPerOz;
}

} // namespace

std::string curve_label(CurveSource source)
{
    switch (source)
    {
    case CurveSource::Own:
        return "measured";
    case CurveSource::Group:
        return "group";
    case CurveSource::Family:
        return "borrowed";
    case CurveSource::Composite:
        return "composite";
    case CurveSource::Flat:
        return "flat";
    }
    return {};
}

double grams_for_ml(const BowlItem &item, double from_g, double ml)
{
    const double start = std::max(0.0, from_g);
    if (ml <= 0)
    {
        return 0.0;
    }
    if (!item.has_curve)
    {
        const double ml_per_g = item.flat_oz_per_100g / 100.0 * kMlPerOz;
        return ml_per_g > 0 ? ml / ml_per_g : 0.0;
    }
    if (item.K <= 0 || item.p <= 0)
    {
        return 0.0;
    }
    // volume = K * g^p, so the mass holding a volume is (volume / K)^(1/p).
    const double target_oz = item.volume_oz(start) + ml / kMlPerOz;
    return std::pow(target_oz / item.K, 1.0 / item.p) - start;
}

std::vector<StepRow> step_table(const Menu &menu,
                                const CurveSet &curves,
                                double target_ml,
                                double flat_protein_rate,
                                double flat_topping_rate)
{
    std::vector<StepRow> rows;
    for (Kind kind : {Kind::Base, Kind::Protein, Kind::Topping})
    {
        for (const std::string &name : menu.names_of(kind))
        {
            const Ingredient *ing = menu.find(name);
            if (!ing)
            {
                continue;
            }
            const BowlItem item = make_item(*ing, curves, flat_protein_rate, flat_topping_rate);
            StepRow row;
            row.ingredient = name;
            row.kind = kind;
            row.source = choose_curve(name, kind, curves).source;
            row.portion_g = ing->full_portion();
            row.step_g = ing->step_increment_g;
            row.marginal_ml_per_g = item.marginal_oz_per_100g(row.portion_g) / 100.0 * kMlPerOz;
            if (row.portion_g > 0)
            {
                row.ml_per_g = ml_at(item, row.portion_g) / row.portion_g;
                row.g_per_ml = row.ml_per_g > 0 ? 1.0 / row.ml_per_g : 0.0;
            }
            row.step_ml = ml_at(item, row.portion_g + row.step_g) - ml_at(item, row.portion_g);
            row.step_g_for_target = grams_for_ml(item, row.portion_g, target_ml);
            row.catalog_ml_at_1_g = (item.has_curve ? item.K : item.flat_oz_per_100g / 100.0) * kMlPerOz;
            row.catalog_volume_exponent = item.has_curve ? item.p : 1.0;
            for (const MenuName &n : menu.names)
            {
                if (n.name == name && n.kind != "sauce")
                {
                    append_unique(row.catalog_names, n.raw);
                }
            }
            rows.push_back(row);
        }
    }
    return rows;
}

std::string step_table_csv(const std::string &brand, const std::vector<StepRow> &rows, double target_ml)
{
    const std::string target = general(target_ml);
    std::string out = std::format("brand,ingredient,kind,curve,portion_g,ml_per_g,g_per_ml,next_g_adds_ml,"
                                  "step_g,step_adds_ml,step_g_for_{}_ml,catalog_ml_at_1_g,catalog_volume_exponent\n",
                                  target);
    for (const StepRow &r : rows)
    {
        const bool portion = r.portion_g > 0;
        out += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
                           csv_escape(brand),
                           csv_escape(r.ingredient),
                           to_string(r.kind),
                           curve_label(r.source),
                           fixed(r.portion_g, 1),
                           portion ? fixed(r.ml_per_g, 3) : "n/a",
                           portion ? fixed(r.g_per_ml, 3) : "n/a",
                           fixed(r.marginal_ml_per_g, 3),
                           fixed(r.step_g, 2),
                           fixed(r.step_ml, 2),
                           fixed(r.step_g_for_target, 2),
                           r.catalog_ml_at_1_g,
                           r.catalog_volume_exponent);
    }
    return out;
}

std::string catalog_curves_tsv(const std::vector<StepRow> &rows)
{
    std::string out = "ingredient_name\tkind\tml_at_1_g\tvolume_exponent\n";
    for (const StepRow &r : rows)
    {
        std::string kind = to_string(r.kind);
        if (!kind.empty())
        {
            kind[0] = static_cast<char>(kind[0] - 'a' + 'A');
        }
        for (const std::string &n : r.catalog_names)
        {
            out += std::format("{}\t{}\t{}\t{}\n", n, kind, r.catalog_ml_at_1_g, r.catalog_volume_exponent);
        }
    }
    return out;
}

} // namespace bowlfill
