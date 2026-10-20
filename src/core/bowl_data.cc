#include "bowl_data.hh"

#include "menu_model.hh"

#include "text.hh"

#include <algorithm>
#include <cmath>
#include <format>

namespace bowlfill
{
namespace
{

enum class Column
{
    Ingredient,
    Ml,
    Oz,
    Cups,
    Lid,
    ScaleTotal,
    Ignored
};

Column column_kind(const std::string &header)
{
    const std::string h = to_lower(trim(header));
    if (h == "ml" || h == "millilitres" || h == "milliliters" || h == "volume_ml" || h == "cc")
    {
        return Column::Ml;
    }
    if (h == "oz" || h == "fl_oz" || h == "ounces" || h == "volume_oz" || h == "volume")
    {
        return Column::Oz;
    }
    if (h == "cups" || h == "sauce_cups")
    {
        return Column::Cups;
    }
    if (h == "lid")
    {
        return Column::Lid;
    }
    if (h == "scale_g" || h == "total_g" || h == "scale_total_g")
    {
        return Column::ScaleTotal;
    }
    if (h.empty() || h == "notes" || h == "note" || h == "comment" || h == "comments")
    {
        return Column::Ignored;
    }
    return Column::Ingredient;
}

double rmse(const std::vector<double> &err)
{
    if (err.empty())
    {
        return 0.0;
    }
    double s = 0.0;
    for (double e : err)
    {
        s += e * e;
    }
    return std::sqrt(s / err.size());
}

} // namespace

int BowlSheet::single_rows() const
{
    return static_cast<int>(
        std::count_if(rows.begin(), rows.end(), [](const BowlRow &r) { return r.items.size() == 1; }));
}

int BowlSheet::mixed_rows() const
{
    return static_cast<int>(rows.size()) - single_rows();
}

std::string bowl_data_format_help()
{
    return std::string("<b>Bowl data CSV</b> &mdash; one row per scanned bowl<br><br>"
                       "One column per ingredient, holding the grams dispensed (leave it blank when the "
                       "ingredient is not in the bowl), then:<br>"
                       "&nbsp;&nbsp;<code>ml</code> &mdash; the scanned volume of the bowl (or "
                       "<code>oz</code>)<br>"
                       "&nbsp;&nbsp;<code>cups</code> &mdash; <i>optional</i>: sauce cups pressed in "
                       "before the scan<br>"
                       "&nbsp;&nbsp;<code>lid</code> &mdash; <i>optional</i>: closed, forced or failed<br>"
                       "&nbsp;&nbsp;<code>scale_g</code> &mdash; <i>optional</i>: the bowl's total weight "
                       "from a trusted scale; the ingredient weights are scaled to add up to it<br>"
                       "&nbsp;&nbsp;<code>notes</code> &mdash; <i>optional</i>, ignored<br><br>"
                       "<b>Put the ingredient columns in dispense order, left to right</b> &mdash; on the "
                       "robot, station order. The first column is the bottom layer, which carries "
                       "everything after it. Names may be typed as the station spells them; "
                       "<i>White Jasmine Rice retherm v11</i> reads as <i>White Jasmine Rice</i>.<br><br>"
                       "<pre>Romaine Base,White Jasmine Rice,Chicken Al Pastor,Pico de Gallo,ml,cups,lid\n"
                       "55,90,100,50,670,2,closed\n"
                       "60,,,,390,,\n"
                       "120,,,,760,,</pre>"
                       "A row with one ingredient is a point on that ingredient's curve: build a ladder "
                       "by dispensing, scanning and dispensing again, one row per scan with the running "
                       "total. Rows with several ingredients fit how much the lower layers are squashed "
                       "by what lands on them.");
}

bool load_bowl_csv(const std::string &path, BowlSheet &out, std::string *error, double empty_bowl_ml)
{
    std::vector<std::string> lines;
    if (!read_lines(path, lines))
    {
        if (error)
        {
            *error = std::format("Could not open {}", path);
        }
        return false;
    }
    if (lines.empty())
    {
        if (error)
        {
            *error = "The file is empty.";
        }
        return false;
    }

    const std::vector<std::string> heads = split_csv(lines.front());
    std::vector<Column> kinds;
    std::vector<std::string> names;
    int ml_col = -1, oz_col = -1, cups_col = -1, lid_col = -1, scale_col = -1;
    for (int i = 0; i < static_cast<int>(heads.size()); ++i)
    {
        const Column k = column_kind(heads[i]);
        kinds.push_back(k);
        names.push_back(k == Column::Ingredient ? clean_ingredient_name(trim(heads[i])) : std::string());
        if (k == Column::Ml)
        {
            ml_col = i;
        }
        if (k == Column::Oz)
        {
            oz_col = i;
        }
        if (k == Column::Cups)
        {
            cups_col = i;
        }
        if (k == Column::Lid)
        {
            lid_col = i;
        }
        if (k == Column::ScaleTotal)
        {
            scale_col = i;
        }
        if (k == Column::Ingredient)
        {
            out.ingredients.push_back(names.back());
        }
    }
    const int vol_col = ml_col >= 0 ? ml_col : oz_col;
    if (vol_col < 0)
    {
        if (error)
        {
            *error = std::format("No volume column: add ml (or oz).\nFound: {}", join(heads, ", "));
        }
        return false;
    }
    if (out.ingredients.empty())
    {
        if (error)
        {
            *error = "No ingredient columns: one column per ingredient, in dispense "
                     "order, holding grams.";
        }
        return false;
    }

    int line_no = 1;
    for (size_t l = 1; l < lines.size(); ++l)
    {
        const std::string &line = lines[l];
        ++line_no;
        if (trim(line).empty())
        {
            continue;
        }
        const std::vector<std::string> cells = split_csv(line);
        auto cell = [&](int i) {
            return i >= 0 && i < static_cast<int>(cells.size()) ? trim(cells[i]) : std::string();
        };

        BowlRow row;
        row.line = line_no;
        std::optional<double> v = parse_double(cell(vol_col));
        // Correct for the camera's zero before judging the row: a small portion can
        // read below zero on the raw scale.
        if (v)
        {
            *v -= ml_col >= 0 ? empty_bowl_ml : empty_bowl_ml / CurveSet::kMlPerFlOz;
        }
        if (v && *v > 0)
        {
            row.ounces = ml_col >= 0 ? *v / CurveSet::kMlPerFlOz : *v;
        }
        row.cups = std::clamp(parse_int(cell(cups_col)).value_or(0), 0, SauceCups::kMax);
        row.lid = to_lower(cell(lid_col));
        for (size_t i = 0; i < kinds.size(); ++i)
        {
            if (kinds[i] != Column::Ingredient)
            {
                continue;
            }
            const std::optional<double> g = parse_double(cell(static_cast<int>(i)));
            if (g && *g > 0)
            {
                row.items.push_back({names[i], *g});
            }
        }
        // A trusted total from a bench scale beats the per-ingredient weights (the
        // robot's puck drifts): keep the robot's split, take the total from the scale.
        const std::optional<double> scale_g = parse_double(cell(scale_col));
        if (scale_g && *scale_g > 0 && !row.items.empty())
        {
            double sum = 0;
            for (const auto &[n, g] : row.items)
            {
                sum += g;
            }
            for (auto &[n, g] : row.items)
            {
                g *= *scale_g / sum;
            }
            ++out.rescaled;
            out.largest_rescale_g = std::max(out.largest_rescale_g, std::fabs(*scale_g - sum));
        }
        if (row.ounces <= 0 || row.items.empty())
        {
            ++out.skipped;
            continue;
        }
        out.rows.push_back(row);
    }
    if (out.rows.empty())
    {
        if (error)
        {
            *error = std::format("No usable rows: each needs a volume and at least one "
                                 "ingredient weight. {} skipped.",
                                 out.skipped);
        }
        return false;
    }
    return true;
}

std::vector<Observation> single_ingredient_points(const BowlSheet &sheet)
{
    std::vector<Observation> out;
    for (const BowlRow &r : sheet.rows)
    {
        if (r.items.size() != 1 || r.cups > 0)
        {
            continue;
        }
        Observation o;
        o.ingredient = r.items.front().first;
        o.grams = r.items.front().second;
        o.ounces = r.ounces;
        out.push_back(o);
    }
    return out;
}

namespace
{

/// The row's ingredients as bowl items, on their chosen curves.
std::vector<BowlItem> row_items(const BowlRow &row,
                                const CurveSet &curves,
                                double flat_protein_rate,
                                double flat_topping_rate,
                                std::vector<std::string> *flat_rate)
{
    std::vector<BowlItem> items;
    for (const auto &[name, grams] : row.items)
    {
        BowlItem it;
        it.name = name;
        it.kind = guess_kind(name);
        it.start_g = grams;
        it.flat_oz_per_100g = it.kind == Kind::Protein ? flat_protein_rate : flat_topping_rate;
        const CurveChoice c = choose_curve(name, it.kind, curves);
        if (c.source != CurveSource::Flat)
        {
            it.has_curve = true;
            it.K = c.K;
            it.p = c.p;
        }
        else if (flat_rate)
        {
            append_unique(*flat_rate, name);
        }
        it.rel_sd = c.rel_sd;
        it.curve_rel_se = c.rel_se;
        items.push_back(it);
    }
    return items;
}

Frame scan_frame(const BowlRow &row,
                 const CurveSet &curves,
                 double load_transfer,
                 double flat_protein_rate,
                 double flat_topping_rate,
                 std::vector<std::string> *flat_rate,
                 std::vector<BowlItem> *items_out)
{
    const std::vector<BowlItem> items = row_items(row, curves, flat_protein_rate, flat_topping_rate, flat_rate);
    SimSettings s;
    s.floor_g = 0.0;
    s.compress = load_transfer > 0;
    s.load_transfer = load_transfer;
    if (items_out)
    {
        *items_out = items;
    }
    return simulate(items, s).first();
}

} // namespace

double predicted_scan_oz(const BowlRow &row,
                         const CurveSet &curves,
                         double load_transfer,
                         double flat_protein_rate,
                         double flat_topping_rate,
                         std::vector<std::string> *flat_rate)
{
    SauceCups cups;
    cups.cups = row.cups;
    return scan_frame(row, curves, load_transfer, flat_protein_rate, flat_topping_rate, flat_rate, nullptr)
               .ounces +
           cups.volume_oz();
}

FillUncertainty predicted_scan(const BowlRow &row, const CurveSet &curves, double load_transfer)
{
    std::vector<BowlItem> items;
    const Frame f = scan_frame(row, curves, load_transfer, kFlatProteinRate, kFlatToppingRate, nullptr, &items);
    // The masses were weighed, so the robot's dispense variation does not apply.
    FillUncertainty u = fill_uncertainty(items, f.per_item_oz, 0, 0, nullptr);
    SauceCups cups;
    cups.cups = row.cups;
    u.median_oz += cups.volume_oz();
    u.p10_oz += cups.volume_oz();
    u.p90_oz += cups.volume_oz();
    return u;
}

RangeCheck check_ranges(const BowlSheet &sheet, const CurveSet &curves, double load_transfer)
{
    RangeCheck c;
    for (const BowlRow &r : sheet.rows)
    {
        if (r.items.size() < 2)
        {
            continue; // single bowls were used to fit the curves
        }
        const FillUncertainty u = predicted_scan(r, curves, load_transfer);
        if (!u.valid)
        {
            continue;
        }
        ++c.bowls;
        if (r.ounces < u.p10_oz)
        {
            ++c.below;
        }
        else if (r.ounces > u.p90_oz)
        {
            ++c.above;
        }
        else
        {
            ++c.inside;
        }
    }
    return c;
}

SquashFit fit_squash(const BowlSheet &sheet,
                     const CurveSet &curves,
                     double flat_protein_rate,
                     double flat_topping_rate)
{
    SquashFit fit;
    std::vector<const BowlRow *> mixed;
    for (const BowlRow &r : sheet.rows)
    {
        if (r.items.size() >= 2)
        {
            mixed.push_back(&r);
        }
    }
    fit.bowls = static_cast<int>(mixed.size());
    if (mixed.empty())
    {
        return fit;
    }

    auto errors = [&](double phi, std::vector<std::string> *flat = nullptr) {
        std::vector<double> e;
        for (const BowlRow *r : mixed)
        {
            e.push_back(predicted_scan_oz(*r, curves, phi, flat_protein_rate, flat_topping_rate, flat) -
                        r->ounces);
        }
        return e;
    };
    auto sse = [&](double phi) {
        double s = 0.0;
        for (double e : errors(phi))
        {
            s += e * e;
        }
        return s;
    };

    // Coarse grid, then a fine one around the best: predicted volume falls steadily
    // with the factor, so this finds the minimum without an optimiser.
    double best = 0.0, best_sse = sse(0.0);
    for (double phi = 0.05; phi <= kMaxLoadTransfer + 1e-9; phi += 0.05)
    {
        const double s = sse(phi);
        if (s < best_sse)
        {
            best_sse = s;
            best = phi;
        }
    }
    const double lo = std::max(0.0, best - 0.05), hi = std::min(kMaxLoadTransfer, best + 0.05);
    for (double phi = lo; phi <= hi + 1e-12; phi += 0.001)
    {
        const double s = sse(phi);
        if (s < best_sse)
        {
            best_sse = s;
            best = phi;
        }
    }

    fit.valid = true;
    fit.load_transfer = best;
    fit.at_bound = best >= kMaxLoadTransfer - 0.001;
    fit.rmse_uncompressed_oz = rmse(errors(0.0));
    fit.rmse_default_oz = rmse(errors(1.0));
    const std::vector<double> e = errors(best, &fit.flat_rate);
    fit.rmse_fitted_oz = rmse(e);
    double bias = 0.0;
    for (double x : e)
    {
        bias += x;
    }
    fit.mean_bias_oz = bias / e.size();
    return fit;
}

} // namespace bowlfill
