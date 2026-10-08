#include "measurements.hh"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

QByteArray digest(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
}

}  // namespace

int MeasurementSet::failed_files() const
{
    int n = 0;
    for (const MeasurementFile &f : files) n += f.failed;
    return n;
}

bool is_curve_csv(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString header = QTextStream(&file).readLine();
    if (!header.isEmpty() && header[0] == QChar(0xFEFF)) header.remove(0, 1);
    for (const QString &h : header.split(',')) {
        const QString n = h.trimmed().remove('"').toLower();
        if (n == "ingredient" || n == "name") return true;
    }
    return false;
}

int MeasurementSet::dropped_total() const
{
    int n = 0;
    for (const auto &[name, count] : dropped) n += count;
    return n;
}

bool is_reference_name(const QString &ingredient)
{
    return ingredient.trimmed().startsWith("test", Qt::CaseInsensitive);
}

double Backfill::corrected_oz(double reading_oz) const
{
    if (!valid || reading_oz <= 0 || reading_oz >= top_oz) return reading_oz;
    // The reading is what the reference curve gives at some mass; the true volume is
    // that mass at the reference's own density, which the anchor fixes at top_oz.
    return top_oz * std::pow(reading_oz / top_oz, 1.0 / p);
}

double Backfill::factor_at_ml(double ml) const
{
    const double oz = ml / CurveSet::kMlPerFlOz;
    return oz > 0 ? corrected_oz(oz) / oz : 1.0;
}

Backfill fit_backfill(const std::vector<std::pair<double, double>> &reference,
                      const QString &name)
{
    Backfill b;
    b.reference = name;
    b.points = static_cast<int>(reference.size());
    if (b.points < kMinReferencePoints) return b;
    CurveSet c;   // fitted on the volume itself, like every other curve
    double g_max = 0;
    for (const auto &[g, oz] : reference) {
        c.add({"reference", Method::Robot, g, oz});
        g_max = std::max(g_max, g);
    }
    c.refit();
    const Fit *f = c.fit("reference", Method::Robot);
    if (!f) return b;
    b.K = f->K;
    b.p = f->p;
    b.top_oz = f->volume_oz(g_max);
    b.valid = b.top_oz > 0;
    return b;
}

MeasurementSet load_measurements(const QString &dir, CurveSet &curves,
                                 const MeasurementOptions &options)
{
    MeasurementSet set;
    set.dir = dir;
    set.backfill_requested = options.backfill;
    curves.clear();

    const QDir d(dir);
    if (!d.exists()) {
        set.folder_missing = true;
        return set;
    }
    // Read everything first: the backfill is learned from the reference readings,
    // which may sit in any file, before it is applied to the others.
    std::vector<std::pair<size_t, BowlSheet>> sheets;   // (index into set.files, sheet)
    // Hidden files cover editor droppings such as LibreOffice's ".~lock.x.csv#".
    const QStringList names = d.entryList({"*.csv", "*.CSV"}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        MeasurementFile mf;
        mf.name = name;
        const QString path = d.filePath(name);
        QString err;
        if (is_curve_csv(path)) {
            int added = 0;
            const bool ok = curves.load_csv(path, &err, &added);
            mf.rows = added;
            mf.failed = !ok;
            mf.problem = err;
        } else {
            mf.bowl_sheet = true;
            BowlSheet sheet;
            if (!load_bowl_csv(path, sheet, &err, options.empty_bowl_ml)) {
                mf.failed = true;
                mf.problem = err;
            } else {
                mf.rows = static_cast<int>(sheet.rows.size());
                if (sheet.skipped)
                    mf.problem = QString("%1 row%2 skipped (no volume or no weights)")
                                     .arg(sheet.skipped).arg(sheet.skipped == 1 ? "" : "s");
                sheets.push_back({set.files.size(), std::move(sheet)});
            }
        }
        set.files.push_back(mf);
    }

    auto is_reference = [](const BowlRow &r) {
        return r.items.size() == 1 && is_reference_name(r.items.front().first);
    };
    if (options.backfill) {
        std::vector<std::pair<double, double>> ref;
        QStringList ref_names;
        for (const auto &[i, sheet] : sheets)
            for (const BowlRow &r : sheet.rows)
                if (is_reference(r) && r.cups == 0) {
                    ref.push_back({r.items.front().second, r.ounces});
                    if (!ref_names.contains(r.items.front().first))
                        ref_names << r.items.front().first;
                }
        set.backfill = fit_backfill(ref, ref_names.join(", "));
    }

    for (auto &[index, sheet] : sheets) {
        std::vector<BowlRow> kept;
        int dropped_here = 0;
        for (BowlRow r : sheet.rows) {
            if (!is_reference(r)) {
                if (set.backfill.valid) r.ounces = set.backfill.corrected_oz(r.ounces);
                if (options.drop_impossible) {
                    double grams = 0;
                    for (const auto &[n, g] : r.items) grams += g;
                    const double solids_oz = grams / kSolidDensityGPerMl / CurveSet::kMlPerFlOz;
                    if (r.ounces < solids_oz) {
                        ++set.dropped[r.items.size() == 1 ? r.items.front().first
                                                          : QStringLiteral("mixed bowl")];
                        ++dropped_here;
                        continue;
                    }
                }
            }
            kept.push_back(r);
        }
        sheet.rows = kept;
        if (dropped_here) {
            MeasurementFile &mf = set.files[index];
            const QString note = QString("%1 impossible reading%2 ignored")
                                     .arg(dropped_here).arg(dropped_here == 1 ? "" : "s");
            mf.problem = mf.problem.isEmpty() ? note : mf.problem + "; " + note;
        }
        for (const Observation &o : single_ingredient_points(sheet)) curves.add(o);
        for (const QString &n : sheet.ingredients)
            if (!set.bowls.ingredients.contains(n)) set.bowls.ingredients << n;
        set.bowls.rows.insert(set.bowls.rows.end(), sheet.rows.begin(), sheet.rows.end());
        set.bowls.skipped += sheet.skipped;
    }
    curves.refit();
    return set;
}

QString store_measurement_file(const QString &source, const QString &dir, QString *error)
{
    const QFileInfo src(source);
    QDir d(dir);
    if (!d.exists() && !d.mkpath(".")) {
        if (error) *error = QString("Could not create %1").arg(dir);
        return {};
    }
    if (src.absoluteDir() == QDir(d.absolutePath())) return src.absoluteFilePath();

    const QByteArray mine = digest(source);
    QString target = d.filePath(src.fileName());
    for (int n = 2; QFileInfo::exists(target); ++n) {
        if (digest(target) == mine) return target;   // already stored
        target = d.filePath(QString("%1_%2.%3").arg(src.completeBaseName()).arg(n)
                                .arg(src.suffix().isEmpty() ? "csv" : src.suffix()));
    }
    if (!QFile::copy(source, target)) {
        if (error) *error = QString("Could not copy %1 into %2").arg(src.fileName(), dir);
        return {};
    }
    return target;
}

}  // namespace bowlfill
