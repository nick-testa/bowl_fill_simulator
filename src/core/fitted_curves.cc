#include "fitted_curves.hh"

#include "measurements.hh"
#include "text.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <cstdio>
#include <fstream>
#include <map>
#include <tuple>

namespace bowlfill
{
namespace
{

using nlohmann::json;

double number_or(const json &j, const char *key, double fallback)
{
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

std::string text_or(const json &j, const char *key)
{
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

FittedCurve curve_from(const json &j)
{
    FittedCurve c;
    c.name = text_or(j, "name");
    c.group = text_or(j, "group");
    c.K = number_or(j, "k_oz", 0.0);
    c.p = number_or(j, "p", 1.0);
    c.n = static_cast<int>(number_or(j, "n", 0));
    c.batches = static_cast<int>(number_or(j, "batches", 0));
    c.lo_g = number_or(j, "lo_g", 0.0);
    c.hi_g = number_or(j, "hi_g", 0.0);
    c.scatter_rel_sd = number_or(j, "scatter_rel_sd", -1.0);
    c.new_batch_rel_sd = number_or(j, "new_batch_rel_sd", -1.0);
    return c;
}

} // namespace

std::string fit_input_csv(const CurveSet &curves)
{
    struct Row
    {
        std::string key, ingredient, group, batch;
        double g, ml;
    };
    std::vector<Row> rows;
    for (const Observation &o : curves.observations())
    {
        if (is_reference_name(o.ingredient) || o.grams <= 0 || o.ounces <= 0)
        {
            continue;
        }
        rows.push_back({to_lower(o.ingredient),
                        o.ingredient,
                        curves.group_of(o.ingredient),
                        o.batch,
                        o.grams,
                        o.ounces * CurveSet::kMlPerFlOz});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return std::tie(a.key, a.batch, a.g, a.ml) < std::tie(b.key, b.batch, b.g, b.ml);
    });
    // The first spelling of an ingredient names it throughout, as CurveSet does.
    std::map<std::string, std::string> spelling;
    for (const Observation &o : curves.observations())
    {
        spelling.try_emplace(to_lower(o.ingredient), o.ingredient);
    }
    std::string out = "ingredient,group,batch,g,ml\n";
    for (const Row &r : rows)
    {
        out += std::format("{},{},{},{:.3f},{:.3f}\n",
                           csv_field(spelling.at(r.key)),
                           csv_field(r.group),
                           csv_field(r.batch),
                           r.g,
                           r.ml);
    }
    return out;
}

std::string fit_fingerprint(const std::string &fit_input)
{
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : fit_input)
    {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return std::format("{:016x}", h);
}

bool write_fit_input(const std::string &path, const CurveSet &curves, std::string *error)
{
    const std::string body = fit_input_csv(curves);
    const std::string text = "# fingerprint: " + fit_fingerprint(body) + "\n" + body;
    if (path == "-")
    {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return true;
    }
    if (!write_text(path, text))
    {
        if (error)
        {
            *error = std::format("could not write {}", path);
        }
        return false;
    }
    return true;
}

bool load_fitted_curves(const std::string &path, FittedCurves &out, std::string *error)
{
    out = FittedCurves{};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        return true;
    }
    std::ifstream in(path);
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        if (error)
        {
            *error = std::format("{} is not valid JSON; rerun {}", path, refit_command());
        }
        return false;
    }
    if (number_or(j, "schema", 0) != 1)
    {
        if (error)
        {
            *error = std::format("{} has an unknown schema; rerun {}", path, refit_command());
        }
        return false;
    }
    out.fingerprint = text_or(j, "fit_input_fingerprint");
    out.fitted_at = text_or(j, "fitted_at");
    out.batch_rel_sd = number_or(j, "batch_rel_sd", 0.0);
    if (auto d = j.find("diagnostics"); d != j.end() && d->is_object())
    {
        out.divergences = static_cast<int>(number_or(*d, "divergences", 0));
        out.max_rhat = number_or(*d, "max_rhat", 0.0);
    }
    for (const char *key : {"ingredients", "groups"})
    {
        auto list = j.find(key);
        if (list == j.end() || !list->is_array())
        {
            continue;
        }
        for (const json &c : *list)
        {
            if (c.is_object() && !text_or(c, "name").empty())
            {
                (std::string(key) == "ingredients" ? out.ingredients : out.groups).push_back(curve_from(c));
            }
        }
    }
    out.loaded = true;
    return true;
}

FitStatus fit_status(const CurveSet &curves)
{
    FitStatus s;
    const FittedCurves &f = curves.fitted();
    if (!f.loaded)
    {
        return s;
    }
    s.state = f.fingerprint == fit_fingerprint(fit_input_csv(curves)) ? FitState::Current : FitState::Stale;
    for (const std::string &n : curves.ingredients())
    {
        if (is_reference_name(n))
        {
            continue;
        }
        const bool covered = std::any_of(
            f.ingredients.begin(), f.ingredients.end(), [&](const FittedCurve &c) { return iequals(c.name, n); });
        if (!covered && curves.count(n) > 0)
        {
            s.unfitted.push_back(n);
        }
    }
    return s;
}

std::string refit_command()
{
    return "uv run tools/fit_curves.py";
}

std::string fit_status_message(const FitStatus &status, const FittedCurves &fitted)
{
    switch (status.state)
    {
    case FitState::Current:
        return {};
    case FitState::Missing:
        return std::format("No fitted curves (data/fitted_curves.json): every curve is least squares on its own "
                           "points, with no batch-to-batch uncertainty. Run {} to fit them.",
                           refit_command());
    case FitState::Stale:
    {
        std::string s = std::format("Fitted curves are out of date: the measurements or pool groups changed since "
                                    "they were fitted{}. They are still in use",
                                    fitted.fitted_at.empty() ? "" : " (" + fitted.fitted_at + ")");
        if (!status.unfitted.empty())
        {
            s += std::format(", and {} {} least squares until refitted",
                             join(status.unfitted, ", "),
                             status.unfitted.size() == 1 ? "uses" : "use");
        }
        return s + std::format(". Run {} to refit.", refit_command());
    }
    }
    return {};
}

} // namespace bowlfill
