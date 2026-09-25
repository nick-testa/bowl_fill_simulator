#include "curves.hh"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

QString normalise(QString s) { return s.trimmed().toLower(); }

Method method_from(const QString &raw)
{
    const QString s = normalise(raw);
    if (s.startsWith("hand")) return Method::Hand;
    return Method::Robot;
}

/// Canonical spellings for the three ingredients the built-in set covers, so a CSV
/// saying "kale" or "Massaged Kale" lands in the same bucket.
QString canonical_ingredient(const QString &raw)
{
    const QString s = normalise(raw);
    if (s == "kale" || s == "massaged kale") return "Massaged Kale";
    if (s == "romaine" || s == "romaine base") return "Romaine Base";
    if (s == "mexican rice" || s == "mexicanrice") return "Mexican Rice";
    QString out = raw.trimmed();
    if (!out.isEmpty()) out[0] = out[0].toUpper();
    return out;
}

/// Splits a CSV line, honouring double quotes so a quoted ingredient name
/// containing a comma survives.
QStringList split_csv(const QString &line)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
            else quoted = !quoted;
        } else if (c == ',' && !quoted) {
            out << cur; cur.clear();
        } else {
            cur += c;
        }
    }
    out << cur;
    return out;
}

Fit fit_power_law(const std::vector<const Observation *> &pts)
{
    Fit f;
    f.n = static_cast<int>(pts.size());
    if (f.n < 2) return f;

    // OLS of log(oz) on log(g).
    double sx = 0, sy = 0;
    for (const Observation *o : pts) {
        if (o->grams <= 0 || o->ounces <= 0) return f;
        sx += std::log(o->grams);
        sy += std::log(o->ounces);
    }
    const double mx = sx / f.n, my = sy / f.n;
    double sxx = 0, sxy = 0;
    for (const Observation *o : pts) {
        const double dx = std::log(o->grams) - mx;
        sxx += dx * dx;
        sxy += dx * (std::log(o->ounces) - my);
    }
    if (sxx <= 0) return f;                 // every point at the same mass

    f.p = sxy / sxx;
    f.K = std::exp(my - f.p * mx);

    // R^2 reported on the ounces scale, not the log scale, so it means what a
    // reader expects when comparing against the linear alternative.
    double ss_tot = 0, ss_res = 0, mean_oz = 0;
    for (const Observation *o : pts) mean_oz += o->ounces;
    mean_oz /= f.n;
    for (const Observation *o : pts) {
        const double pred = f.K * std::pow(o->grams, f.p);
        ss_tot += (o->ounces - mean_oz) * (o->ounces - mean_oz);
        ss_res += (o->ounces - pred) * (o->ounces - pred);
    }
    f.r2 = ss_tot > 0 ? 1.0 - ss_res / ss_tot : 0.0;

    auto [lo, hi] = std::minmax_element(
        pts.begin(), pts.end(),
        [](const Observation *a, const Observation *b) { return a->grams < b->grams; });
    f.lo_g = (*lo)->grams;
    f.hi_g = (*hi)->grams;
    f.valid = true;
    return f;
}

}  // namespace

QString to_string(Method method)
{
    switch (method) {
    case Method::Robot: return "robot";
    case Method::Hand: return "hand";
    case Method::Pooled: return "pooled";
    }
    return {};
}

double Fit::volume_oz(double grams) const
{
    return (!valid || grams <= 0) ? 0.0 : K * std::pow(grams, p);
}

double Fit::marginal_oz_per_100g(double grams) const
{
    return (!valid || grams <= 0) ? 0.0 : K * p * std::pow(grams, p - 1.0) * 100.0;
}

QStringList CurveSet::required_columns() { return {"ingredient", "g", "oz"}; }
// 1 US fluid ounce, exact by definition.
const double CurveSet::kMlPerFlOz = 29.5735295625;
QStringList CurveSet::optional_columns() { return {"method"}; }

QString CurveSet::format_help()
{
    return QStringLiteral(
        "<b>Mass-to-volume CSV</b><br><br>"
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
        "<code>robot</code> or <code>hand</code>, defaults to robot<br><br>"
        "<pre>method,ingredient,g,oz\n"
        "robot,romaine,86,20\n"
        "robot,kale,73,25\n"
        "hand,mexican rice,150,11</pre>"
        "Rows are <b>added</b> to the built-in 36 bowls and the affected curves are "
        "refitted. An ingredient not seen before gets a curve of its own once it has "
        "at least two points at different masses.<br><br>"
        "Names are matched loosely: <i>kale</i>, <i>Kale</i> and <i>Massaged Kale</i> "
        "all land in the same bucket.");
}

void CurveSet::add(const Observation &obs) { obs_.push_back(obs); }

bool CurveSet::load_csv(const QString &path, QString *error, int *rows_added,
                        QStringList *new_ingredients)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Could not open %1").arg(path);
        return false;
    }
    QTextStream in(&file);

    QString header = in.readLine();
    if (header.isNull()) {
        if (error) *error = "The file is empty.";
        return false;
    }
    // Tolerate a UTF-8 BOM, which spreadsheet exports often leave in place.
    if (!header.isEmpty() && header[0] == QChar(0xFEFF)) header.remove(0, 1);

    std::map<QString, int> col;
    const QStringList heads = split_csv(header);
    for (int i = 0; i < heads.size(); ++i) {
        const QString h = normalise(heads[i]);
        if (h == "ingredient" || h == "name") col["ingredient"] = i;
        else if (h == "g" || h == "mass" || h == "grams" || h == "mass_g") col["g"] = i;
        else if (h == "oz" || h == "volume" || h == "ounces" || h == "volume_oz"
                 || h == "fl_oz") col["oz"] = i;
        // A metric sheet is the same measurement in another unit, so take it and
        // convert on the way in; the model stays canonical in fluid ounces.
        else if (h == "ml" || h == "millilitres" || h == "milliliters"
                 || h == "volume_ml" || h == "cc") col["ml"] = i;
        else if (h == "method" || h == "fill" || h == "source") col["method"] = i;
    }
    const bool metric_input = !col.count("oz") && col.count("ml");
    if (metric_input) col["oz"] = col.at("ml");

    QStringList missing;
    for (const QString &need : required_columns())
        if (!col.count(need)) missing << need;
    if (!missing.isEmpty()) {
        if (error)
            *error = QString("Missing required column%1: %2 (or ml).\nFound: %3")
                         .arg(missing.size() > 1 ? "s" : "", missing.join(", "),
                              heads.join(", "));
        return false;
    }

    const QStringList before = ingredients();
    std::vector<Observation> staged;
    int skipped = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        const QStringList cells = split_csv(line);
        const int need = std::max({col.at("ingredient"), col.at("g"), col.at("oz")});
        if (cells.size() <= need) { ++skipped; continue; }

        bool ok_g = false, ok_oz = false;
        Observation o;
        o.ingredient = canonical_ingredient(cells[col.at("ingredient")]);
        o.grams = cells[col.at("g")].trimmed().toDouble(&ok_g);
        o.ounces = cells[col.at("oz")].trimmed().toDouble(&ok_oz);
        if (metric_input) o.ounces /= kMlPerFlOz;
        // method is optional, and a ragged row may simply stop before it.
        o.method = (col.count("method") && col.at("method") < cells.size())
                       ? method_from(cells[col.at("method")])
                       : Method::Robot;

        if (!ok_g || !ok_oz || o.grams <= 0 || o.ounces <= 0 || o.ingredient.isEmpty()) {
            ++skipped;
            continue;
        }
        staged.push_back(o);
    }

    if (staged.empty()) {
        if (error)
            *error = QString("No usable rows. %1 row%2 had a missing or non-positive "
                             "mass or volume.")
                         .arg(skipped).arg(skipped == 1 ? "" : "s");
        return false;
    }

    for (const Observation &o : staged) obs_.push_back(o);
    refit();

    if (rows_added) *rows_added = static_cast<int>(staged.size());
    if (new_ingredients) {
        for (const QString &n : ingredients())
            if (!before.contains(n)) *new_ingredients << n;
    }
    if (error && skipped > 0)
        *error = QString("%1 row%2 skipped as unusable.")
                     .arg(skipped).arg(skipped == 1 ? " was" : "s were");
    return true;
}

void CurveSet::refit()
{
    fits_.clear();
    std::map<QString, std::map<Method, std::vector<const Observation *>>> buckets;
    for (const Observation &o : obs_) {
        buckets[o.ingredient][o.method].push_back(&o);
        buckets[o.ingredient][Method::Pooled].push_back(&o);
    }
    for (const auto &[ingredient, by_method] : buckets)
        for (const auto &[method, pts] : by_method)
            fits_[ingredient][method] = fit_power_law(pts);
}

void CurveSet::reset_to_builtin(const QString &builtin_csv_path)
{
    obs_.clear();
    fits_.clear();
    QString ignored;
    load_csv(builtin_csv_path, &ignored);
}

const Fit *CurveSet::fit(const QString &ingredient, Method method) const
{
    auto i = fits_.find(ingredient);
    if (i == fits_.end()) return nullptr;
    auto j = i->second.find(method);
    if (j == i->second.end() || !j->second.valid) return nullptr;
    return &j->second;
}

QStringList CurveSet::ingredients() const
{
    QStringList out;
    for (const auto &[name, _] : fits_) out << name;
    return out;
}

int CurveSet::count(const QString &ingredient, Method method) const
{
    int n = 0;
    for (const Observation &o : obs_)
        if (o.ingredient == ingredient && (method == Method::Pooled || o.method == method)) ++n;
    return n;
}

QString proxy_curve_for(const QString &ingredient, const CurveSet &curves)
{
    if (curves.fit(ingredient, Method::Pooled)) return {};
    // White Rice and Brown Rice and Lentils were never measured; Mexican Rice is
    // the closest thing that was.
    if (ingredient == "White Rice" || ingredient == "Brown Rice and Lentils") {
        if (curves.fit("Mexican Rice", Method::Pooled)) return "Mexican Rice";
    }
    return {};
}

}  // namespace bowlfill
