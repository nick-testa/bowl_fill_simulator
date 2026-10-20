#include "measurements.hh"

#include "text.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace bowlfill
{
namespace
{

namespace fs = std::filesystem;

/// The whole file, byte for byte; empty when it cannot be read.
std::string contents(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool same_contents(const std::string &a, const std::string &b)
{
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec)
    {
        return false;
    }
    return contents(a) == contents(b);
}

/// Regular, visible .csv files in `dir`, any case, sorted by name.
std::vector<std::string> csv_files(const std::string &dir)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const fs::directory_entry &e : fs::directory_iterator(dir, ec))
    {
        const std::string name = e.path().filename().string();
        if (name.starts_with('.') || !e.is_regular_file(ec))
        {
            continue;
        }
        if (iequals(e.path().extension().string(), ".csv"))
        {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

int MeasurementSet::failed_files() const
{
    int n = 0;
    for (const MeasurementFile &f : files)
    {
        n += f.failed;
    }
    return n;
}

bool is_curve_csv(const std::string &path)
{
    std::vector<std::string> lines;
    if (!read_lines(path, lines) || lines.empty())
    {
        return false;
    }
    for (const std::string &h : split(lines.front(), ','))
    {
        std::string n = trim(h);
        std::erase(n, '"');
        n = to_lower(n);
        if (n == "ingredient" || n == "name")
        {
            return true;
        }
    }
    return false;
}

int MeasurementSet::dropped_total() const
{
    int n = 0;
    for (const auto &[name, count] : dropped)
    {
        n += count;
    }
    return n;
}

bool is_reference_name(const std::string &ingredient)
{
    return istarts_with(trim(ingredient), "test");
}

double Backfill::corrected_oz(double reading_oz) const
{
    if (!valid || reading_oz <= 0 || reading_oz >= top_oz)
    {
        return reading_oz;
    }
    // The reading is what the reference curve gives at some mass; the true volume is
    // that mass at the reference's own density, which the anchor fixes at top_oz.
    return top_oz * std::pow(reading_oz / top_oz, 1.0 / p);
}

double Backfill::factor_at_ml(double ml) const
{
    const double oz = ml / CurveSet::kMlPerFlOz;
    return oz > 0 ? corrected_oz(oz) / oz : 1.0;
}

Backfill fit_backfill(const std::vector<std::pair<double, double>> &reference, const std::string &name)
{
    Backfill b;
    b.reference = name;
    b.points = static_cast<int>(reference.size());
    if (b.points < kMinReferencePoints)
    {
        return b;
    }
    CurveSet c; // fitted on the volume itself, like every other curve
    double g_max = 0;
    for (const auto &[g, oz] : reference)
    {
        c.add({"reference", g, oz});
        g_max = std::max(g_max, g);
    }
    c.refit();
    const Fit *f = c.fit("reference");
    if (!f)
    {
        return b;
    }
    b.K = f->K;
    b.p = f->p;
    b.top_oz = f->volume_oz(g_max);
    b.valid = b.top_oz > 0;
    return b;
}

MeasurementSet load_measurements(const std::string &dir, CurveSet &curves, const MeasurementOptions &options)
{
    MeasurementSet set;
    set.dir = dir;
    set.backfill_requested = options.backfill;
    curves.clear();

    std::error_code ec;
    if (!fs::is_directory(dir, ec))
    {
        set.folder_missing = true;
        return set;
    }
    // Read everything first: the backfill is learned from the reference readings,
    // which may sit in any file, before it is applied to the others.
    std::vector<std::pair<size_t, BowlSheet>> sheets; // (index into set.files, sheet)
    // Hidden files cover editor droppings such as LibreOffice's ".~lock.x.csv#".
    for (const std::string &name : csv_files(dir))
    {
        MeasurementFile mf;
        mf.name = name;
        const std::string path = (fs::path(dir) / name).string();
        std::string err;
        if (is_curve_csv(path))
        {
            int added = 0;
            const bool ok = curves.load_csv(path, &err, &added);
            mf.rows = added;
            mf.failed = !ok;
            mf.problem = err;
        }
        else
        {
            mf.bowl_sheet = true;
            BowlSheet sheet;
            if (!load_bowl_csv(path, sheet, &err, options.empty_bowl_ml))
            {
                mf.failed = true;
                mf.problem = err;
            }
            else
            {
                mf.rows = static_cast<int>(sheet.rows.size());
                if (sheet.skipped)
                {
                    mf.problem = std::format(
                        "{} row{} skipped (no volume or no weights)", sheet.skipped, sheet.skipped == 1 ? "" : "s");
                }
                if (sheet.rescaled)
                {
                    const std::string note = std::format("{} row{} scaled to scale_g (up to {} g)",
                                                         sheet.rescaled,
                                                         sheet.rescaled == 1 ? "" : "s",
                                                         fixed(sheet.largest_rescale_g, 1));
                    mf.problem = mf.problem.empty() ? note : mf.problem + "; " + note;
                }
                sheets.push_back({set.files.size(), std::move(sheet)});
            }
        }
        set.files.push_back(mf);
    }

    auto is_reference = [](const BowlRow &r) {
        return r.items.size() == 1 && is_reference_name(r.items.front().first);
    };
    if (options.backfill)
    {
        std::vector<std::pair<double, double>> ref;
        std::vector<std::string> ref_names;
        for (const auto &[i, sheet] : sheets)
        {
            for (const BowlRow &r : sheet.rows)
            {
                if (is_reference(r) && r.cups == 0)
                {
                    ref.push_back({r.items.front().second, r.ounces});
                    append_unique(ref_names, r.items.front().first);
                }
            }
        }
        set.backfill = fit_backfill(ref, join(ref_names, ", "));
    }

    for (auto &[index, sheet] : sheets)
    {
        std::vector<BowlRow> kept;
        int dropped_here = 0;
        for (BowlRow r : sheet.rows)
        {
            if (!is_reference(r))
            {
                if (set.backfill.valid)
                {
                    r.ounces = set.backfill.corrected_oz(r.ounces);
                }
                if (options.drop_impossible)
                {
                    double grams = 0;
                    for (const auto &[n, g] : r.items)
                    {
                        grams += g;
                    }
                    const double solids_oz = grams / kSolidDensityGPerMl / CurveSet::kMlPerFlOz;
                    if (r.ounces < solids_oz)
                    {
                        ++set.dropped[r.items.size() == 1 ? r.items.front().first : std::string("mixed bowl")];
                        ++dropped_here;
                        continue;
                    }
                }
            }
            kept.push_back(r);
        }
        sheet.rows = kept;
        if (dropped_here)
        {
            MeasurementFile &mf = set.files[index];
            const std::string note =
                std::format("{} impossible reading{} ignored", dropped_here, dropped_here == 1 ? "" : "s");
            mf.problem = mf.problem.empty() ? note : mf.problem + "; " + note;
        }
        for (const Observation &o : single_ingredient_points(sheet))
        {
            curves.add(o);
        }
        for (const std::string &n : sheet.ingredients)
        {
            append_unique(set.bowls.ingredients, n);
        }
        set.bowls.rows.insert(set.bowls.rows.end(), sheet.rows.begin(), sheet.rows.end());
        set.bowls.skipped += sheet.skipped;
    }
    curves.refit();
    return set;
}

std::string store_measurement_file(const std::string &source, const std::string &dir, std::string *error)
{
    std::error_code ec;
    const fs::path src = fs::absolute(source, ec).lexically_normal();
    const fs::path d = fs::absolute(dir, ec).lexically_normal();
    if (!fs::is_directory(d, ec) && !fs::create_directories(d, ec))
    {
        if (error)
        {
            *error = std::format("Could not create {}", dir);
        }
        return {};
    }
    if (fs::equivalent(src.parent_path(), d, ec))
    {
        return src.string();
    }

    // A name taken by a different file gets a numbered suffix; one taken by the same
    // file means it is already stored.
    const std::string stem = src.stem().string();
    const std::string suffix = src.has_extension() ? src.extension().string().substr(1) : "csv";
    fs::path target = d / src.filename();
    for (int n = 2; fs::exists(target, ec); ++n)
    {
        if (same_contents(source, target.string()))
        {
            return target.string();
        }
        target = d / std::format("{}_{}.{}", stem, n, suffix);
    }
    if (!fs::copy_file(source, target, ec))
    {
        if (error)
        {
            *error = std::format("Could not copy {} into {}", src.filename().string(), dir);
        }
        return {};
    }
    return target.string();
}

} // namespace bowlfill
