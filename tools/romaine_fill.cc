// Romaine fill sweep: how much romaine fills a bowl to the target fraction when
// everything else is held at its menu portion.
//
// For every protein and topping combination a brand admits, romaine is the free
// variable and is solved for directly. The dynamic portion ramp is deliberately not
// modelled: these are recipes built from scratch, and the results are meant to inform
// step values later, not to reproduce today's ramp.
//
//   target:  food volume + sauce cup volume = fill_pct x bowl capacity
//
// Romaine is base 1, the bottom layer, so with compression on it carries everything
// dispensed after it. A second base, when present, sits on top of romaine at half its
// full portion -- halved by mass, or halved by volume (the grams that occupy half the
// full portion's unloaded volume, which is a deeper cut for compacting grains).
//
// Every solved bowl is re-run through simulate() with no floor, and the tool fails
// loudly if the simulator's volume disagrees with the solve.

#include "core/menu_model.hh"
#include "core/simulator.hh"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <tuple>

using namespace bowlfill;

namespace {

const QString kRomaine = "Romaine Base";

/// Every subset of {0..n-1} with between lo and hi members.
std::vector<std::vector<int>> subsets(int n, int lo, int hi)
{
    std::vector<std::vector<int>> out;
    std::vector<int> pick;
    const int top = std::min(hi, n);
    std::function<void(int)> walk = [&](int start) {
        if (static_cast<int>(pick.size()) >= lo) out.push_back(pick);
        if (static_cast<int>(pick.size()) == top) return;
        for (int i = start; i < n; ++i) {
            pick.push_back(i);
            walk(i + 1);
            pick.pop_back();
        }
    };
    walk(0);
    return out;
}

QString csv(const QString &s)
{
    return s.contains(',') || s.contains('"') ? "\"" + QString(s).replace("\"", "\"\"") + "\""
                                              : s;
}

/// Food volume of a bowl, as simulate() computes it with the ramp switched off.
double food_oz(std::vector<BowlItem> items, bool compress)
{
    SimSettings s;
    s.floor_g = 0.0;
    s.compress = compress;
    return simulate(std::move(items), s).first().ounces;
}

/// Romaine volume as the bottom layer under `load_g` of everything else. Mirrors the
/// loaded_oz lambda in simulate(); every solve is checked against simulate() itself.
double romaine_oz(const BowlItem &r, double g, double load_g, bool compress)
{
    if (!compress || g <= 0) return r.volume_oz(g);
    const double e = std::min(r.p - 1.0, 0.0);
    return g * r.K * std::pow(2.0, e) * std::pow(g / 2.0 + load_g, e);
}

struct Summary {
    std::vector<double> grams;   ///< solved romaine per combination, NaN when no room
    int no_room = 0;
};

double quantile(std::vector<double> v, double q)
{
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    const double pos = q * (v.size() - 1);
    const size_t i = static_cast<size_t>(pos);
    const double f = pos - i;
    return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Solve for the romaine weight that fills each menu combination to a target "
        "fraction of the bowl, with no dynamic portion ramp.");
    parser.addHelpOption();
    QCommandLineOption assets("assets", "Directory holding menus/ and data/.", "dir",
                              BOWLFILL_ASSET_DIR);
    QCommandLineOption brand("brand", "One brand, or 'all' (default).", "name", "all");
    QCommandLineOption fill("fill", "Target fill as a fraction of the bowl (default 0.85).",
                            "f", "0.85");
    QCommandLineOption cap("capacity", "Bowl capacity in fl oz (default 32).", "oz", "32");
    QCommandLineOption min_pro("min-proteins", "Fewest proteins (default 1).", "n", "1");
    QCommandLineOption max_pro("max-proteins", "Most proteins (default 2).", "n", "2");
    QCommandLineOption min_top("min-toppings", "Fewest toppings (default 1).", "n", "1");
    QCommandLineOption max_top("max-toppings", "Most toppings (default 4).", "n", "4");
    QCommandLineOption summary_out("summary", "Summary CSV path (default stdout).", "file");
    QCommandLineOption detail_out("detail", "Also write one row per combination here.",
                                  "file");
    parser.addOptions({assets, brand, fill, cap, min_pro, max_pro, min_top, max_top,
                       summary_out, detail_out});
    parser.process(app);

    QTextStream err(stderr);
    const QString asset_dir = QDir(parser.value(assets)).absolutePath();

    CurveSet curves;
    QString error;
    if (!curves.load_csv(asset_dir + "/data/mass_to_volume.csv", &error)) {
        err << "curves: " << error << "\n";
        return 2;
    }
    // Same name mappings and piece heights the app applies, so names agree with it.
    NameMappings maps;
    PieceHeights pieces;
    load_name_mappings(asset_dir + "/data/ingredient_aliases.csv", maps, &error);
    load_piece_heights(asset_dir + "/data/ingredient_pieces.csv", pieces, &error);
    const LoadResult loaded = load_menus(asset_dir + "/menus", &maps, &pieces);

    const double fill_frac = parser.value(fill).toDouble();
    const double capacity = parser.value(cap).toDouble();
    const int p_lo = parser.value(min_pro).toInt(), p_hi = parser.value(max_pro).toInt();
    const int t_lo = parser.value(min_top).toInt(), t_hi = parser.value(max_top).toInt();
    const QString want = parser.value(brand);

    const Fit *rfit = curves.fit(kRomaine, Method::Robot);
    if (!rfit) {
        err << "no robot curve for " << kRomaine << "\n";
        return 2;
    }

    QFile detail_file;
    QTextStream detail;
    const bool want_detail = parser.isSet(detail_out);
    if (want_detail) {
        detail_file.setFileName(parser.value(detail_out));
        if (!detail_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err << "cannot write " << detail_file.fileName() << "\n";
            return 2;
        }
        detail.setDevice(&detail_file);
        detail << "brand,base_config,second_base,second_base_g,split_mode,proteins,"
                  "toppings,n_proteins,n_toppings,sauce_cups,compress,other_food_oz,"
                  "other_food_g,romaine_g,romaine_oz,fill_pct,status\n";
    }

    // brand, base_config, second_base, split_mode, n_proteins, n_toppings, cups, compress
    using Key = std::tuple<QString, QString, QString, QString, int, int, int, bool>;
    std::map<Key, Summary> summary;
    qint64 rows = 0, mismatches = 0;
    bool matched = false;

    for (const Menu &menu : loaded.menus) {
        if (want.compare("all", Qt::CaseInsensitive) != 0
            && menu.brand.compare(want, Qt::CaseInsensitive) != 0)
            continue;
        const Ingredient *romaine = menu.find(kRomaine);
        if (!romaine) {
            err << menu.brand << ": no " << kRomaine << ", skipped\n";
            continue;
        }
        matched = true;
        const BowlItem rproto = make_item(*romaine, curves, Method::Robot);

        std::vector<const Ingredient *> proteins, toppings;
        for (const QString &n : menu.names_of(Kind::Protein)) proteins.push_back(menu.find(n));
        for (const QString &n : menu.names_of(Kind::Topping)) toppings.push_back(menu.find(n));

        // Base configurations: romaine alone, then romaine under each other base at
        // half its portion, halved by mass and by volume.
        struct BaseCfg {
            QString label, second, split;
            std::optional<BowlItem> item;
        };
        std::vector<BaseCfg> base_cfgs{{"single", "", "n/a", std::nullopt}};
        for (const QString &n : menu.names_of(Kind::Base)) {
            if (n == kRomaine) continue;
            const Ingredient *b = menu.find(n);
            if (!b || b->full_portion() <= 0) continue;
            BowlItem it = make_item(*b, curves, Method::Robot);
            const double full = b->full_portion();
            BowlItem by_mass = it, by_vol = it;
            by_mass.start_g = full * 0.5;
            // Unloaded volume is K*g^p, so half the volume is full * 0.5^(1/p).
            by_vol.start_g = it.has_curve ? full * std::pow(0.5, 1.0 / it.p) : full * 0.5;
            base_cfgs.push_back({"double", n, "half_mass", by_mass});
            base_cfgs.push_back({"double", n, "half_volume", by_vol});
        }

        QHash<QString, BowlItem> proto;
        for (const auto *pool : {&proteins, &toppings})
            for (const Ingredient *i : *pool) {
                BowlItem it = make_item(*i, curves, Method::Robot);
                it.start_g = i->full_portion();
                proto.insert(i->name, it);
            }

        const auto p_sets = subsets(static_cast<int>(proteins.size()), p_lo, p_hi);
        const auto t_sets = subsets(static_cast<int>(toppings.size()), t_lo, t_hi);
        err << menu.brand << ": " << proteins.size() << " proteins, " << toppings.size()
            << " toppings, " << base_cfgs.size() << " base configs, "
            << p_sets.size() * t_sets.size() << " protein/topping combinations\n";

        std::vector<BowlItem> others;
        for (const BaseCfg &bc : base_cfgs)
        for (const auto &ps : p_sets)
        for (const auto &ts : t_sets) {
            // Dispense order is stacking order: romaine, second base, proteins, toppings.
            others.clear();
            if (bc.item) others.push_back(*bc.item);
            QStringList pn, tn;
            for (int i : ps) { others.push_back(proto.value(proteins[i]->name)); pn << proteins[i]->name; }
            for (int i : ts) { others.push_back(proto.value(toppings[i]->name)); tn << toppings[i]->name; }
            double load_g = 0.0;
            for (const BowlItem &o : others) load_g += o.start_g;

            for (bool compress : {false, true}) {
                const double rest_oz = food_oz(others, compress);
                for (int cups = 1; cups <= SauceCups::kMax; ++cups) {
                    SauceCups sc;
                    sc.cups = cups;
                    const double room = fill_frac * capacity - sc.volume_oz() - rest_oz;

                    double g = 0.0;
                    QString status = "ok";
                    if (room <= 0) {
                        status = "no_room";
                    } else {
                        // Romaine volume is monotone in its own mass, so bisect.
                        double lo = 0.0, hi = 1.0;
                        while (romaine_oz(rproto, hi, load_g, compress) < room) hi *= 2;
                        for (int it = 0; it < 80; ++it) {
                            const double mid = (lo + hi) / 2;
                            (romaine_oz(rproto, mid, load_g, compress) < room ? lo : hi) = mid;
                        }
                        g = (lo + hi) / 2;
                        if (g < rfit->lo_g || g > rfit->hi_g) status = "extrapolated";
                    }

                    // Check the solve against the simulator itself.
                    std::vector<BowlItem> bowl;
                    BowlItem r = rproto;
                    r.start_g = g;
                    bowl.push_back(r);
                    bowl.insert(bowl.end(), others.begin(), others.end());
                    const double food = food_oz(bowl, compress);
                    const double fill_pct = (food + sc.volume_oz()) / capacity * 100;
                    if (status != "no_room" && std::abs(food - (rest_oz + room)) > 0.01)
                        ++mismatches;

                    Summary &s = summary[{menu.brand, bc.label, bc.second,
                                          bc.label == "single"
                                                                    ? QString("n/a")
                                                                    : bc.split,
                                          static_cast<int>(ps.size()),
                                          static_cast<int>(ts.size()), cups, compress}];
                    if (status == "no_room") ++s.no_room;
                    else s.grams.push_back(g);
                    ++rows;

                    if (want_detail) {
                        detail << csv(menu.brand) << ',' << bc.label << ',' << csv(bc.second)
                               << ','
                               << (bc.item ? QString::number(bc.item->start_g, 'f', 1) : "")
                               << ',' << bc.split << ',' << csv(pn.join('|')) << ','
                               << csv(tn.join('|')) << ',' << ps.size() << ',' << ts.size()
                               << ',' << cups << ',' << (compress ? 1 : 0) << ','
                               << QString::number(rest_oz, 'f', 2) << ','
                               << QString::number(load_g, 'f', 0) << ','
                               << QString::number(g, 'f', 1) << ','
                               << QString::number(food - rest_oz, 'f', 2) << ','
                               << QString::number(fill_pct, 'f', 1) << ',' << status << '\n';
                    }
                }
            }
        }
    }
    if (!matched) {
        err << "no brand matching \"" << want << "\" with " << kRomaine << "\n";
        return 2;
    }

    QFile sfile;
    QTextStream out(stdout);
    if (parser.isSet(summary_out)) {
        sfile.setFileName(parser.value(summary_out));
        if (!sfile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            err << "cannot write " << sfile.fileName() << "\n";
            return 2;
        }
        out.setDevice(&sfile);
    }
    // min_g is the guard: the heaviest combination at that count still fits with it.
    out << "brand,base_config,second_base,split_mode,n_proteins,n_toppings,sauce_cups,compress,"
           "combinations,no_room,min_g,p10_g,median_g,p90_g,max_g\n";
    for (const auto &[k, s] : summary) {
        const auto &[b, cfg, second, split, np, nt, cups, compress] = k;
        auto f = [](double v) { return std::isnan(v) ? QString() : QString::number(v, 'f', 1); };
        const double mn = s.grams.empty() ? NAN : *std::min_element(s.grams.begin(), s.grams.end());
        const double mx = s.grams.empty() ? NAN : *std::max_element(s.grams.begin(), s.grams.end());
        out << csv(b) << ',' << cfg << ',' << csv(second) << ',' << split << ',' << np << ',' << nt << ',' << cups
            << ',' << (compress ? 1 : 0) << ',' << s.grams.size() + s.no_room << ','
            << s.no_room << ',' << (s.no_room ? QString("0") : f(mn)) << ','
            << f(quantile(s.grams, 0.10)) << ',' << f(quantile(s.grams, 0.5)) << ','
            << f(quantile(s.grams, 0.90)) << ',' << f(mx) << '\n';
    }
    out.flush();

    err << rows << " bowls solved; romaine curve measured " << rfit->lo_g << "-" << rfit->hi_g
        << " g (K=" << rfit->K << ", p=" << rfit->p << "); " << mismatches
        << " disagreed with simulate()\n";
    return mismatches ? 1 : 0;
}
