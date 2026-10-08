#include "bowl_data.hh"

#include "menu_model.hh"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

/// Splits a CSV line, honouring double quotes so a quoted name with a comma survives.
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
            out << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    out << cur;
    return out;
}

enum class Column { Ingredient, Ml, Oz, Cups, Lid, Ignored };

Column column_kind(const QString &header)
{
    const QString h = header.trimmed().toLower();
    if (h == "ml" || h == "millilitres" || h == "milliliters" || h == "volume_ml" || h == "cc")
        return Column::Ml;
    if (h == "oz" || h == "fl_oz" || h == "ounces" || h == "volume_oz" || h == "volume")
        return Column::Oz;
    if (h == "cups" || h == "sauce_cups") return Column::Cups;
    if (h == "lid") return Column::Lid;
    if (h.isEmpty() || h == "notes" || h == "note" || h == "comment" || h == "comments")
        return Column::Ignored;
    return Column::Ingredient;
}

double rmse(const std::vector<double> &err)
{
    if (err.empty()) return 0.0;
    double s = 0.0;
    for (double e : err) s += e * e;
    return std::sqrt(s / err.size());
}

}  // namespace

int BowlSheet::single_rows() const
{
    return static_cast<int>(std::count_if(rows.begin(), rows.end(),
                                           [](const BowlRow &r) { return r.items.size() == 1; }));
}

int BowlSheet::mixed_rows() const
{
    return static_cast<int>(rows.size()) - single_rows();
}

QString bowl_data_format_help()
{
    return QStringLiteral(
        "<b>Bowl data CSV</b> &mdash; one row per scanned bowl<br><br>"
        "One column per ingredient, holding the grams dispensed (leave it blank when the "
        "ingredient is not in the bowl), then:<br>"
        "&nbsp;&nbsp;<code>ml</code> &mdash; the scanned volume of the bowl (or "
        "<code>oz</code>)<br>"
        "&nbsp;&nbsp;<code>cups</code> &mdash; <i>optional</i>: sauce cups pressed in "
        "before the scan<br>"
        "&nbsp;&nbsp;<code>lid</code> &mdash; <i>optional</i>: closed, forced or failed<br>"
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

bool load_bowl_csv(const QString &path, BowlSheet &out, QString *error, double empty_bowl_ml)
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
    if (!header.isEmpty() && header[0] == QChar(0xFEFF)) header.remove(0, 1);

    const QStringList heads = split_csv(header);
    std::vector<Column> kinds;
    std::vector<QString> names;
    int ml_col = -1, oz_col = -1, cups_col = -1, lid_col = -1;
    for (int i = 0; i < heads.size(); ++i) {
        const Column k = column_kind(heads[i]);
        kinds.push_back(k);
        names.push_back(k == Column::Ingredient ? clean_ingredient_name(heads[i].trimmed())
                                                : QString());
        if (k == Column::Ml) ml_col = i;
        if (k == Column::Oz) oz_col = i;
        if (k == Column::Cups) cups_col = i;
        if (k == Column::Lid) lid_col = i;
        if (k == Column::Ingredient) out.ingredients << names.back();
    }
    const int vol_col = ml_col >= 0 ? ml_col : oz_col;
    if (vol_col < 0) {
        if (error) *error = QString("No volume column: add ml (or oz).\nFound: %1")
                                .arg(heads.join(", "));
        return false;
    }
    if (out.ingredients.isEmpty()) {
        if (error) *error = "No ingredient columns: one column per ingredient, in dispense "
                            "order, holding grams.";
        return false;
    }

    int line_no = 1;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++line_no;
        if (line.trimmed().isEmpty()) continue;
        const QStringList cells = split_csv(line);
        auto cell = [&](int i) { return i >= 0 && i < cells.size() ? cells[i].trimmed() : QString(); };

        BowlRow row;
        row.line = line_no;
        bool ok = false;
        double v = cell(vol_col).toDouble(&ok);
        // Correct for the camera's zero before judging the row: a small portion can
        // read below zero on the raw scale.
        if (ok) v -= ml_col >= 0 ? empty_bowl_ml : empty_bowl_ml / CurveSet::kMlPerFlOz;
        if (ok && v > 0) row.ounces = ml_col >= 0 ? v / CurveSet::kMlPerFlOz : v;
        row.cups = std::clamp(cell(cups_col).toInt(), 0, SauceCups::kMax);
        row.lid = cell(lid_col).toLower();
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (kinds[i] != Column::Ingredient) continue;
            const double g = cell(static_cast<int>(i)).toDouble(&ok);
            if (ok && g > 0) row.items.push_back({names[i], g});
        }
        if (row.ounces <= 0 || row.items.empty()) {
            ++out.skipped;
            continue;
        }
        out.rows.push_back(row);
    }
    if (out.rows.empty()) {
        if (error) *error = QString("No usable rows: each needs a volume and at least one "
                                    "ingredient weight. %1 skipped.").arg(out.skipped);
        return false;
    }
    return true;
}

std::vector<Observation> single_ingredient_points(const BowlSheet &sheet)
{
    std::vector<Observation> out;
    for (const BowlRow &r : sheet.rows) {
        if (r.items.size() != 1 || r.cups > 0) continue;
        Observation o;
        o.ingredient = r.items.front().first;
        o.method = Method::Robot;
        o.grams = r.items.front().second;
        o.ounces = r.ounces;
        out.push_back(o);
    }
    return out;
}

double predicted_scan_oz(const BowlRow &row, const CurveSet &curves, Method method,
                         double load_transfer, double flat_protein_rate,
                         double flat_topping_rate, QStringList *flat_rate)
{
    std::vector<BowlItem> items;
    for (const auto &[name, grams] : row.items) {
        BowlItem it;
        it.name = name;
        it.kind = guess_kind(name);
        it.start_g = grams;
        it.flat_oz_per_100g = it.kind == Kind::Protein ? flat_protein_rate : flat_topping_rate;
        const CurveChoice c = choose_curve(name, it.kind, curves, method);
        if (c.source != CurveSource::Flat) {
            it.has_curve = true;
            it.K = c.K;
            it.p = c.p;
        } else if (flat_rate && !flat_rate->contains(name)) {
            *flat_rate << name;
        }
        items.push_back(it);
    }
    SimSettings s;
    s.floor_g = 0.0;
    s.compress = true;
    s.load_transfer = load_transfer;
    SauceCups cups;
    cups.cups = row.cups;
    return simulate(items, s).first().ounces + cups.volume_oz();
}

SquashFit fit_squash(const BowlSheet &sheet, const CurveSet &curves, Method method,
                     double flat_protein_rate, double flat_topping_rate)
{
    SquashFit fit;
    std::vector<const BowlRow *> mixed;
    for (const BowlRow &r : sheet.rows)
        if (r.items.size() >= 2) mixed.push_back(&r);
    fit.bowls = static_cast<int>(mixed.size());
    if (mixed.empty()) return fit;

    auto errors = [&](double phi, QStringList *flat = nullptr) {
        std::vector<double> e;
        for (const BowlRow *r : mixed)
            e.push_back(predicted_scan_oz(*r, curves, method, phi, flat_protein_rate,
                                          flat_topping_rate, flat)
                        - r->ounces);
        return e;
    };
    auto sse = [&](double phi) {
        double s = 0.0;
        for (double e : errors(phi)) s += e * e;
        return s;
    };

    // Coarse grid, then a fine one around the best: predicted volume falls steadily
    // with the factor, so this finds the minimum without an optimiser.
    double best = 0.0, best_sse = sse(0.0);
    for (double phi = 0.05; phi <= kMaxLoadTransfer + 1e-9; phi += 0.05) {
        const double s = sse(phi);
        if (s < best_sse) { best_sse = s; best = phi; }
    }
    const double lo = std::max(0.0, best - 0.05), hi = std::min(kMaxLoadTransfer, best + 0.05);
    for (double phi = lo; phi <= hi + 1e-12; phi += 0.001) {
        const double s = sse(phi);
        if (s < best_sse) { best_sse = s; best = phi; }
    }

    fit.valid = true;
    fit.load_transfer = best;
    fit.at_bound = best >= kMaxLoadTransfer - 0.001;
    fit.rmse_uncompressed_oz = rmse(errors(0.0));
    fit.rmse_default_oz = rmse(errors(1.0));
    const std::vector<double> e = errors(best, &fit.flat_rate);
    fit.rmse_fitted_oz = rmse(e);
    double bias = 0.0;
    for (double x : e) bias += x;
    fit.mean_bias_oz = bias / e.size();
    return fit;
}

}  // namespace bowlfill
