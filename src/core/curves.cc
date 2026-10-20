#include "curves.hh"

#include "menu_model.hh"
#include "text.hh"

#include <algorithm>
#include <cmath>
#include <format>
#include <regex>

namespace bowlfill
{
namespace
{

std::string normalise(const std::string &s)
{
    return to_lower(trim(s));
}

bool is_hand(const std::string &raw)
{
    return normalise(raw).starts_with("hand");
}

/// Canonical spellings for the three ingredients the built-in set covers, so a CSV
/// saying "kale" or "Massaged Kale" lands in the same bucket.
std::string canonical_ingredient(const std::string &raw)
{
    const std::string s = normalise(raw);
    if (s == "kale" || s == "massaged kale")
    {
        return "Massaged Kale";
    }
    if (s == "romaine" || s == "romaine base")
    {
        return "Romaine Base";
    }
    if (s == "mexican rice" || s == "mexicanrice")
    {
        return "Mexican Rice";
    }
    std::string out = trim(raw);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
    {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

/// oz = K * g^p by least squares, either on the volume itself or on log-log.
Fit fit_power_law(const std::vector<const Observation *> &pts, FitSpace space)
{
    Fit f;
    f.n = static_cast<int>(pts.size());
    if (f.n < 2)
    {
        return f;
    }
    for (const Observation *o : pts)
    {
        if (o->grams <= 0 || o->ounces <= 0)
        {
            return f;
        }
    }
    auto [lo, hi] = std::minmax_element(
        pts.begin(), pts.end(), [](const Observation *a, const Observation *b) { return a->grams < b->grams; });
    if ((*hi)->grams <= (*lo)->grams)
    {
        return f; // every point at the same mass
    }

    if (space == FitSpace::Log)
    {
        // OLS of log(oz) on log(g).
        double sx = 0, sy = 0;
        for (const Observation *o : pts)
        {
            sx += std::log(o->grams);
            sy += std::log(o->ounces);
        }
        const double mx = sx / f.n, my = sy / f.n;
        double sxx = 0, sxy = 0;
        for (const Observation *o : pts)
        {
            const double dx = std::log(o->grams) - mx;
            sxx += dx * dx;
            sxy += dx * (std::log(o->ounces) - my);
        }
        f.p = sxy / sxx;
        f.K = std::exp(my - f.p * mx);
    }
    else
    {
        // Least squares on the volume. For a given p the best K is closed-form,
        // K = sum(v g^p) / sum(g^2p), leaving a one-dimensional search over p for
        // the smallest squared error, sum(v^2) - sum(v g^p)^2 / sum(g^2p).
        // Masses are scaled by the largest so the powers stay well conditioned.
        const double gmax = (*hi)->grams;
        double svv = 0;
        for (const Observation *o : pts)
        {
            svv += o->ounces * o->ounces;
        }
        auto sse = [&](double p, double *k) {
            double svg = 0, sgg = 0;
            for (const Observation *o : pts)
            {
                const double gp = std::pow(o->grams / gmax, p);
                svg += o->ounces * gp;
                sgg += gp * gp;
            }
            if (k)
            {
                *k = svg / sgg;
            }
            return svv - svg * svg / sgg;
        };
        constexpr double kLo = 0.2, kHi = 3.0;
        double best_p = 1.0, best = sse(1.0, nullptr);
        for (double p = kLo; p <= kHi + 1e-9; p += 0.01)
        {
            const double e = sse(p, nullptr);
            if (e < best)
            {
                best = e;
                best_p = p;
            }
        }
        // Golden-section search inside the best grid cell.
        double a = std::max(kLo, best_p - 0.01), b = std::min(kHi, best_p + 0.01);
        const double r = (std::sqrt(5.0) - 1) / 2;
        double c = b - r * (b - a), d = a + r * (b - a);
        for (int i = 0; i < 60; ++i)
        {
            if (sse(c, nullptr) < sse(d, nullptr))
            {
                b = d;
            }
            else
            {
                a = c;
            }
            c = b - r * (b - a);
            d = a + r * (b - a);
        }
        f.p = (a + b) / 2;
        double k_scaled = 0;
        sse(f.p, &k_scaled);
        if (k_scaled <= 0)
        {
            return f;
        }
        f.K = k_scaled / std::pow(gmax, f.p); // back to unscaled grams
    }

    // R^2 reported on the ounces scale, not the log scale, so it means what a
    // reader expects when comparing against the linear alternative.
    double ss_tot = 0, ss_res = 0, mean_oz = 0;
    for (const Observation *o : pts)
    {
        mean_oz += o->ounces;
    }
    mean_oz /= f.n;
    for (const Observation *o : pts)
    {
        const double pred = f.K * std::pow(o->grams, f.p);
        ss_tot += (o->ounces - mean_oz) * (o->ounces - mean_oz);
        ss_res += (o->ounces - pred) * (o->ounces - pred);
    }
    f.r2 = ss_tot > 0 ? 1.0 - ss_res / ss_tot : 0.0;
    // Scatter as a share of the prediction: spread grows with portion size, so a
    // relative figure carries from 50 g to 200 g where an absolute one would not.
    // Two parameters are fitted, so n - 2 degrees of freedom remain.
    if (f.n > 2)
    {
        double ss_rel = 0;
        for (const Observation *o : pts)
        {
            const double r = o->ounces / (f.K * std::pow(o->grams, f.p)) - 1.0;
            ss_rel += r * r;
        }
        f.rel_sd = std::sqrt(ss_rel / (f.n - 2));
    }
    f.lo_g = (*lo)->grams;
    f.hi_g = (*hi)->grams;
    f.valid = true;
    return f;
}

} // namespace

double Fit::volume_oz(double grams) const
{
    return (!valid || grams <= 0) ? 0.0 : K * std::pow(grams, p);
}

double Fit::marginal_oz_per_100g(double grams) const
{
    return (!valid || grams <= 0) ? 0.0 : K * p * std::pow(grams, p - 1.0) * 100.0;
}

std::vector<std::string> CurveSet::required_columns()
{
    return {"ingredient", "g", "oz"};
}

// 1 US fluid ounce, exact by definition.
const double CurveSet::kMlPerFlOz = 29.5735295625;

std::vector<std::string> CurveSet::optional_columns()
{
    return {"method"};
}

std::string CurveSet::format_help()
{
    return std::string("<b>Mass-to-volume CSV</b><br><br>"
                       "A header row naming these columns, in any order and any case:<br>"
                       "&nbsp;&nbsp;<code>ingredient</code> &mdash; e.g. Massaged Kale, Romaine Base<br>"
                       "&nbsp;&nbsp;<code>g</code> &mdash; mass in grams (also accepts "
                       "<code>mass</code>, <code>grams</code>, <code>mass_g</code>)<br>"
                       "&nbsp;&nbsp;<code>oz</code> &mdash; volume in fluid ounces (also accepts "
                       "<code>volume</code>, <code>ounces</code>, <code>volume_oz</code>)<br>"
                       "&nbsp;&nbsp;<code>ml</code> &mdash; <i>or</i> volume in millilitres (also "
                       "<code>millilitres</code>, <code>volume_ml</code>, <code>cc</code>), converted "
                       "on import<br>"
                       "&nbsp;&nbsp;<code>method</code> &mdash; <i>optional</i>: "
                       "<code>robot</code> (the default) or <code>hand</code>; hand-filled rows are "
                       "skipped, as curves are robot-only<br><br>"
                       "<pre>method,ingredient,g,oz\n"
                       "robot,romaine,86,20\n"
                       "robot,kale,73,25</pre>"
                       "Rows are <b>added</b> to the measurements and the affected curves are "
                       "refitted. An ingredient not seen before gets a curve of its own once it has "
                       "at least two points at different masses.<br><br>"
                       "Names are matched loosely: <i>kale</i>, <i>Kale</i> and <i>Massaged Kale</i> "
                       "all land in the same bucket.");
}

void CurveSet::add(const Observation &obs)
{
    obs_.push_back(obs);
}

bool CurveSet::load_csv(const std::string &path,
                        std::string *error,
                        int *rows_added,
                        std::vector<std::string> *new_ingredients,
                        int *hand_rows_skipped)
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

    std::map<std::string, int> col;
    const std::vector<std::string> heads = split_csv(lines.front());
    for (int i = 0; i < static_cast<int>(heads.size()); ++i)
    {
        const std::string h = normalise(heads[i]);
        if (h == "ingredient" || h == "name")
        {
            col["ingredient"] = i;
        }
        else if (h == "g" || h == "mass" || h == "grams" || h == "mass_g")
        {
            col["g"] = i;
        }
        else if (h == "oz" || h == "volume" || h == "ounces" || h == "volume_oz" || h == "fl_oz")
        {
            col["oz"] = i;
        }
        // A metric sheet is the same measurement in another unit, so take it and
        // convert on the way in; the model stays canonical in fluid ounces.
        else if (h == "ml" || h == "millilitres" || h == "milliliters" || h == "volume_ml" || h == "cc")
        {
            col["ml"] = i;
        }
        else if (h == "method" || h == "fill" || h == "source")
        {
            col["method"] = i;
        }
    }
    const bool metric_input = !col.count("oz") && col.count("ml");
    if (metric_input)
    {
        col["oz"] = col.at("ml");
    }

    std::vector<std::string> missing;
    for (const std::string &need : required_columns())
    {
        if (!col.count(need))
        {
            missing.push_back(need);
        }
    }
    if (!missing.empty())
    {
        if (error)
        {
            *error = std::format("Missing required column{}: {} (or ml).\nFound: {}",
                                 missing.size() > 1 ? "s" : "",
                                 join(missing, ", "),
                                 join(heads, ", "));
        }
        return false;
    }

    const std::vector<std::string> before = ingredients();
    std::vector<Observation> staged;
    int skipped = 0, hand = 0;
    for (size_t l = 1; l < lines.size(); ++l)
    {
        const std::string &line = lines[l];
        if (trim(line).empty())
        {
            continue;
        }
        const std::vector<std::string> cells = split_csv(line);
        const int need = std::max({col.at("ingredient"), col.at("g"), col.at("oz")});
        if (static_cast<int>(cells.size()) <= need)
        {
            ++skipped;
            continue;
        }

        Observation o;
        o.ingredient = canonical_ingredient(cells[col.at("ingredient")]);
        o.batch = path.substr(path.find_last_of("/\\") + 1);
        const std::optional<double> g = parse_double(cells[col.at("g")]);
        const std::optional<double> oz = parse_double(cells[col.at("oz")]);
        o.grams = g.value_or(0.0);
        o.ounces = oz.value_or(0.0);
        if (metric_input)
        {
            o.ounces /= kMlPerFlOz;
        }
        // method is optional, and a ragged row may simply stop before it.
        if (col.count("method") && col.at("method") < static_cast<int>(cells.size()) &&
            is_hand(cells[col.at("method")]))
        {
            ++hand;
            continue;
        }

        if (!g || !oz || o.grams <= 0 || o.ounces <= 0 || o.ingredient.empty())
        {
            ++skipped;
            continue;
        }
        staged.push_back(o);
    }

    if (hand_rows_skipped)
    {
        *hand_rows_skipped = hand;
    }
    const std::string hand_note =
        hand ? std::format("{} hand-filled row{} skipped: curves are robot-only.", hand, hand == 1 ? " was" : "s were")
             : std::string();
    if (staged.empty())
    {
        if (error)
        {
            *error = std::format("No usable rows. {} row{} had a missing or non-positive "
                                 "mass or volume.",
                                 skipped,
                                 skipped == 1 ? "" : "s");
            if (!hand_note.empty())
            {
                *error += " " + hand_note;
            }
        }
        return false;
    }

    for (const Observation &o : staged)
    {
        obs_.push_back(o);
    }
    refit();

    if (rows_added)
    {
        *rows_added = static_cast<int>(staged.size());
    }
    if (new_ingredients)
    {
        for (const std::string &n : ingredients())
        {
            if (!contains(before, n))
            {
                new_ingredients->push_back(n);
            }
        }
    }
    if (error && (skipped > 0 || hand > 0))
    {
        std::vector<std::string> notes;
        if (skipped > 0)
        {
            notes.push_back(std::format("{} row{} skipped as unusable.", skipped, skipped == 1 ? " was" : "s were"));
        }
        if (!hand_note.empty())
        {
            notes.push_back(hand_note);
        }
        *error = join(notes, " ");
    }
    return true;
}

void CurveSet::refit()
{
    fits_.clear();
    display_.clear();
    std::map<std::string, std::vector<const Observation *>> buckets;
    for (const Observation &o : obs_)
    {
        // The first spelling seen names the curve; later ones in other cases join it.
        const std::string &name = display_.try_emplace(to_lower(o.ingredient), o.ingredient).first->second;
        buckets[name].push_back(&o);
    }
    for (const auto &[ingredient, pts] : buckets)
    {
        fits_[ingredient] = fit_power_law(pts, space_);
    }

    // Fitted curves, with R^2 and the measured range read off this ingredient's points.
    fitted_fits_.clear();
    group_fits_.clear();
    auto as_fit = [](const FittedCurve &c) {
        Fit f;
        f.K = c.K;
        f.p = c.p;
        f.n = c.n;
        f.lo_g = c.lo_g;
        f.hi_g = c.hi_g;
        f.rel_sd = c.scatter_rel_sd;
        f.new_batch_rel_sd = c.new_batch_rel_sd;
        f.fitted = true;
        f.valid = c.K > 0 && c.p > 0;
        return f;
    };
    for (const FittedCurve &c : fitted_.ingredients)
    {
        Fit f = as_fit(c);
        if (auto b = buckets.find(display_.count(to_lower(c.name)) ? display_.at(to_lower(c.name)) : c.name);
            b != buckets.end() && b->second.size() > 1)
        {
            double mean = 0, ss_tot = 0, ss_res = 0;
            for (const Observation *o : b->second)
            {
                mean += o->ounces;
            }
            mean /= b->second.size();
            for (const Observation *o : b->second)
            {
                ss_tot += (o->ounces - mean) * (o->ounces - mean);
                ss_res += (o->ounces - f.volume_oz(o->grams)) * (o->ounces - f.volume_oz(o->grams));
            }
            f.r2 = ss_tot > 0 ? 1.0 - ss_res / ss_tot : 0.0;
        }
        fitted_fits_[to_lower(c.name)] = f;
    }
    for (const FittedCurve &c : fitted_.groups)
    {
        if (c.n > 0)
        {
            group_fits_[to_lower(c.name)] = as_fit(c);
        }
    }
}

void CurveSet::set_fitted(FittedCurves fitted)
{
    fitted_ = std::move(fitted);
    refit();
}

void CurveSet::set_groups(std::map<std::string, std::string> groups)
{
    groups_ = std::move(groups);
}

std::string CurveSet::group_of(const std::string &ingredient) const
{
    return pooling_group(ingredient, groups_);
}

const Fit *CurveSet::group_fit(const std::string &group) const
{
    auto g = group_fits_.find(to_lower(group));
    return g != group_fits_.end() && g->second.valid ? &g->second : nullptr;
}

void CurveSet::set_fit_space(FitSpace space)
{
    if (space == space_)
    {
        return;
    }
    space_ = space;
    refit();
}

void CurveSet::clear()
{
    obs_.clear();
    fits_.clear();
    display_.clear();
}

void CurveSet::reset_to_builtin(const std::string &builtin_csv_path)
{
    clear();
    std::string ignored;
    load_csv(builtin_csv_path, &ignored);
}

const Fit *CurveSet::fit(const std::string &ingredient) const
{
    if (auto f = fitted_fits_.find(to_lower(ingredient)); f != fitted_fits_.end() && f->second.valid)
    {
        return &f->second;
    }
    auto d = display_.find(to_lower(ingredient));
    if (d == display_.end())
    {
        return nullptr;
    }
    auto i = fits_.find(d->second);
    if (i == fits_.end() || !i->second.valid)
    {
        return nullptr;
    }
    return &i->second;
}

std::vector<std::string> CurveSet::ingredients() const
{
    std::vector<std::string> out;
    for (const auto &[name, _] : fits_)
    {
        out.push_back(name);
    }
    return out;
}

int CurveSet::count(const std::string &ingredient) const
{
    int n = 0;
    for (const Observation &o : obs_)
    {
        if (iequals(o.ingredient, ingredient))
        {
            ++n;
        }
    }
    return n;
}

std::string proxy_curve_for(const std::string &ingredient, const CurveSet &curves)
{
    if (curves.fit(ingredient))
    {
        return {};
    }

    // An ingredient with no data of its own borrows a measured one from its family:
    // White Jasmine Rice or Brown Rice and Lentils borrow a measured rice, and the
    // retired plain Couscous borrows Lemon Couscous, its near-identical successor.
    // Within a family, a preferred member is taken first when it has data.
    struct Family
    {
        std::regex word;
        const char *preferred;
    };

    static const Family kFamilies[] = {{std::regex(R"(\brice\b)", std::regex::icase), "Mexican Rice"},
                                       {std::regex(R"(\bcouscous\b)", std::regex::icase), nullptr}};
    for (const Family &f : kFamilies)
    {
        if (!std::regex_search(ingredient, f.word))
        {
            continue;
        }
        if (f.preferred && curves.fit(f.preferred))
        {
            return f.preferred;
        }
        for (const std::string &n : curves.ingredients())
        {
            if (std::regex_search(n, f.word) && curves.fit(n))
            {
                return n;
            }
        }
    }
    return {};
}

} // namespace bowlfill
