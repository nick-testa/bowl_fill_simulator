#include "menu_model.hh"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <map>

namespace bowlfill {
namespace {

const QStringList kBases = {"Romaine Base", "Massaged Kale", "White Rice",
                            "Brown Rice and Lentils", "Mexican Rice"};
const QStringList kProteins = {"Chicken Herby", "Chicken Tex-Mex", "Braised Beef",
                               "Beef Shawarma", "Broccoli", "Seared Pork"};

///
/// The menu never says what kind an ingredient is, and brand refreshes rename
/// everything ("White Rice" becomes "White Jasmine Rice retherm v11"), so beyond the
/// names above the kind is read from words in the name. A prepared salad stays a
/// topping even when it names a meat.
///
const QSet<QString> kGreenWords = {"romaine", "kale", "lettuce", "greens", "spinach",
                                   "arugula"};
const QSet<QString> kGrainWords = {"rice", "quinoa", "farro", "grains"};
const QSet<QString> kProteinWords = {"chicken", "beef", "pork", "steak", "carnitas",
                                     "suadero", "pastor", "shawarma", "barbacoa",
                                     "chorizo", "lamb", "turkey", "tofu", "salmon",
                                     "shrimp", "fish"};
const QSet<QString> kNotProteinWords = {"salad", "broth", "stock"};

/// Portioned into cups rather than dispensed into the bowl. The simulator carries
/// sauce as SauceCups, so these must never be swept or dispensed as toppings. An
/// explicit list from the culinary team, not a keyword guess: Seeds and Cotija go in
/// cups, and nothing about their names says so. Matched as a whole phrase in the
/// lower-cased name, so a brand's spelling ("Cantina Salsa Roja", "Green Salsa
/// Macha") still lands; a per-brand mapping with kind "sauce" or "topping" overrides.
const QSet<QString> kSauceNames = {"ranch",        "salsa matcha",   "chipotle crema",
                                   "citrus vin",   "salsa macha",    "seeds",
                                   "tzatziki",     "cilantro crema", "harissa",
                                   "salsa roja",   "sour cream",     "cotija",
                                   "chili crisp",  "miso ginger",    "spicy tahini",
                                   "guacachile salsa", "salsa cremosa"};

QSet<QString> tokens(const QString &text);
QString clean_name(const QString &raw);

bool mentions(const QSet<QString> &toks, const QSet<QString> &words)
{
    for (const QString &t : toks)
        if (words.contains(t)) return true;
    return false;
}

/// Bases split into two families with very different portion sizes; a grain must
/// never borrow a leaf's weight, so the class fallback stays inside the family.
QString family_of(const QString &name)
{
    if (name == "Romaine Base" || name == "Massaged Kale") return "green";
    if (kBases.contains(name)) return "grain";
    const QSet<QString> toks = tokens(name);
    if (mentions(toks, kGreenWords)) return "green";
    if (mentions(toks, kGrainWords)) return "grain";
    return {};
}

Kind kind_of(const QString &name)
{
    if (kBases.contains(name)) return Kind::Base;
    if (kProteins.contains(name)) return Kind::Protein;
    const QSet<QString> toks = tokens(name);
    if (mentions(toks, kGreenWords) || mentions(toks, kGrainWords)) return Kind::Base;
    if (mentions(toks, kProteinWords) && !mentions(toks, kNotProteinWords))
        return Kind::Protein;
    return Kind::Topping;
}

/// Checked after kind_of, so a "Ranch Chicken" would still be a protein.
bool is_sauce(const QString &name)
{
    if (kind_of(name) != Kind::Topping) return false;
    const QString padded = " " + clean_name(name).toLower() + " ";
    for (const QString &sauce : kSauceNames)
        if (padded.contains(" " + sauce + " ")) return true;
    return false;
}

///
/// The name an ingredient is offered under. Menus spell one ingredient several ways
/// across stores and recipe revisions ("White Jasmine Rice Retherm", "White Jasmine
/// Rice, retherm v1.1", "White Jasmine Rice retherm v11"); prep state, portioning and
/// version words say how it was made, not what it is, so they are dropped and the
/// spellings collapse onto one option.
///
QString clean_name(const QString &raw)
{
    static const QRegularExpression brackets(R"(\[[^\]]*\]|\([^)]*\))");
    static const QRegularExpression prep(
        R"(\b(retherm|rethermed|cooked|portion|portioned|v\d+(\.\d+)*)\b)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression punct(R"([^\w\s'&+-])");
    static const QRegularExpression space(R"(\s+)");
    QString s = raw;
    s.remove(brackets);
    s.remove(prep);
    s.replace(punct, " ");
    s = s.replace(space, " ").trimmed();
    return s.isEmpty() ? raw.trimmed() : s;
}

const QSet<QString> kStopWords = {"base", "and", "a",  "the", "plain", "single",
                                  "portion", "mb", "m", "no",  "bowl"};

QSet<QString> tokens(const QString &text)
{
    QSet<QString> out;
    static const QRegularExpression split("[^A-Za-z0-9]+");
    for (const QString &part : text.toLower().split(split, Qt::SkipEmptyParts))
        if (!kStopWords.contains(part)) out.insert(part);
    return out;
}

bool is_guid(const QString &s)
{
    static const QRegularExpression re(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return re.match(s).hasMatch();
}

///
/// Reduce a menu_item_id to the words naming the ingredient or dish. Three shapes
/// occur and the name sits in a different place in each:
///   mb:white-rice-a-<32hex>-cddd:<guid>                        -> leading words
///   mb-pt:mb:<template>:<guid>:mexican-rice                    -> trailing segment
///   mb:mb-pt-mb-<template>-<guid>-brown-rice-and-lentils:<guid> -> after an inline guid
///
QString slug_of(const QString &menu_item_id)
{
    QString body = menu_item_id.startsWith("mb:") ? menu_item_id.section(':', 1) : menu_item_id;

    QString keep;
    const int last = body.lastIndexOf(':');
    if (last >= 0) {
        const QString tail = body.mid(last + 1);
        if (is_guid(tail)) {
            body = body.left(last);
        } else {
            keep = tail;
            body = body.left(last);
        }
    }

    static const QRegularExpression long_hash("-a-[0-9a-f]{20,}.*$");
    body.remove(long_hash);
    static const QRegularExpression inline_guid(
        "-?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    body.replace(inline_guid, "-");
    static const QRegularExpression short_prefix("^m-[a-z]{2,4}-");
    body.remove(short_prefix);

    return keep.isEmpty() ? body : body + "-" + keep;
}

QString title_from_slug(const QString &s)
{
    static const QRegularExpression split("[^A-Za-z0-9]+");
    QStringList words;
    for (QString w : s.split(split, Qt::SkipEmptyParts)) {
        w[0] = w[0].toUpper();
        words << w;
    }
    return words.join(' ');
}

/// True when every token of `needle` appears in `haystack`, i.e. the name is fully
/// spelled out inside the slug.
bool covers(const QSet<QString> &needle, const QSet<QString> &haystack)
{
    if (needle.isEmpty()) return false;
    for (const QString &t : needle)
        if (!haystack.contains(t)) return false;
    return true;
}

struct WeightedSlug {
    QSet<QString> toks;
    double grams = 0.0;
};

QJsonObject read_dump(const QString &path, QStringList &warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        warnings << QString("could not open %1").arg(path);
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    // The tool writes "ID: <env>\nMAPPINGS:\n" ahead of the JSON body.
    const int marker = text.indexOf("MAPPINGS:");
    const QString body = marker >= 0 ? text.mid(marker + 9) : text;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &err);
    if (doc.isNull()) {
        warnings << QString("%1: %2").arg(QFileInfo(path).fileName(), err.errorString());
        return {};
    }
    return doc.object();
}

}  // namespace

QString to_string(Kind kind)
{
    switch (kind) {
    case Kind::Base: return "base";
    case Kind::Protein: return "protein";
    case Kind::Topping: return "topping";
    }
    return {};
}

QString to_string(WeightSource source)
{
    switch (source) {
    case WeightSource::Menu: return "menu";
    case WeightSource::Slug: return "slug";
    case WeightSource::Alias: return "alias";
    case WeightSource::Template: return "template";
    case WeightSource::Class: return "inferred";
    case WeightSource::Recipe: return "recipe";
    case WeightSource::None: return "none";
    }
    return {};
}

bool FloorRule::matches(const QStringList &bowl) const
{
    for (const QString &n : must_not_have)
        if (bowl.contains(n)) return false;
    if (must_have.isEmpty()) return true;
    int count = 0;
    for (const QString &n : bowl)
        if (must_have.contains(n)) ++count;
    return op == FilterOp::ExactlyOne ? count == 1 : count > 0;
}

const Ingredient *Menu::find(const QString &name) const
{
    auto it = ingredients.find(name);
    return it == ingredients.end() ? nullptr : &it->second;
}

QStringList Menu::sauces_in(const Recipe &recipe) const
{
    QStringList out;
    for (const RecipeItem &ri : recipe.items)
        if (is_sauce(ri.name) && !out.contains(ri.name)) out << ri.name;
    return out;
}

QStringList Menu::names_of(Kind kind) const
{
    QStringList out;
    for (const auto &[name, ing] : ingredients)
        if (ing.kind == kind) out << name;
    return out;
}

const FloorRule *Menu::floor_for(const QStringList &bowl) const
{
    for (const FloorRule &f : floors)
        if (f.matches(bowl)) return &f;
    return nullptr;
}

//
// ############################################################################
//

namespace {

///
/// Every ingredient name any brand configures. Meat + Rice has no
/// dynamic_portion_increases at all, so its template targets can only be named by
/// borrowing the vocabulary of the other brands.
///
/// The name a brand's menu spelling is offered under: the person's mapping if there
/// is one, otherwise the automatic clean-up.
QString mapped_name(const NameMappings *mappings, const QString &brand, const QString &raw)
{
    if (mappings) {
        auto it = mappings->find({brand, raw});
        if (it != mappings->end() && !it->second.name.trimmed().isEmpty())
            return it->second.name.trimmed();
    }
    return clean_name(raw);
}

QStringList canonical_names(const std::vector<QJsonObject> &raw, const QStringList &brands,
                            const NameMappings *mappings)
{
    QSet<QString> names;
    for (size_t b = 0; b < raw.size(); ++b) {
        const QJsonObject &m = raw[b];
        for (const QJsonValue d : m["dynamic_portion_increases"].toArray())
            for (const QJsonValue i : d["ingredients"].toArray())
                names.insert(mapped_name(mappings, brands[b], i["ingredient_name"].toString()));
        for (const QJsonValue i : m["ingredients"].toArray())
            names.insert(mapped_name(mappings, brands[b], i["ingredient_name"].toString()));
    }
    return QStringList(names.begin(), names.end());
}

Menu build_menu(const QJsonObject &m, const QString &brand, const QStringList &canon,
                const NameMappings *mappings, QStringList &issues)
{
    Menu menu;
    menu.brand = brand;

    // ---- names: every spelling goes through here ---------------------------
    // Kind overrides are keyed by the name in use, so two spellings mapped onto one
    // name share whichever kind was set for either.
    std::map<QString, QString> kind_override;
    if (mappings)
        for (const auto &[key, mapping] : *mappings)
            if (key.first == brand && !mapping.kind.isEmpty())
                kind_override[mapped_name(mappings, brand, key.second)] = mapping.kind;

    auto kind_for = [&](const QString &name) {
        auto it = kind_override.find(name);
        if (it == kind_override.end()) return kind_of(name);
        if (it->second == "base") return Kind::Base;
        if (it->second == "protein") return Kind::Protein;
        return Kind::Topping;   // topping, and sauce (which never reaches a pool)
    };
    auto sauce_for = [&](const QString &name) {
        auto it = kind_override.find(name);
        return it == kind_override.end() ? is_sauce(name) : it->second == "sauce";
    };
    auto add_sauce = [&](const QString &name) {
        if (!menu.sauces.contains(name)) menu.sauces << name;
    };

    std::map<QString, MenuName> seen;   // by raw spelling
    auto name_of = [&](const QString &raw) {
        const QString name = mapped_name(mappings, brand, raw);
        if (!seen.count(raw)) {
            MenuName n;
            n.raw = raw;
            n.automatic = clean_name(raw);
            n.name = name;
            seen[raw] = n;
        }
        return name;
    };
    menu.version = m["menu_package_version"].toObject()["version_identifier"].toString();

    // Ramp settings, in the order the menu lists them. Farmstand names Broccoli
    // twice with different caps; the first entry wins.
    QStringList order;
    for (const QJsonValue d : m["dynamic_portion_increases"].toArray()) {
        for (const QJsonValue iv : d["ingredients"].toArray()) {
            const QJsonObject i = iv.toObject();
            const QString name = name_of(i["ingredient_name"].toString());
            if (sauce_for(name)) {
                add_sauce(name);
                continue;
            }
            if (menu.ingredients.count(name)) continue;
            Ingredient ing;
            ing.name = name;
            ing.kind = kind_for(name);
            ing.step_increment_g = i["step_increment_g"].toDouble();
            ing.max_dispense_weight_g = i["max_dispense_weight_g"].toDouble();
            menu.ingredients[name] = ing;
            order << name;
        }
    }

    // mappings.ingredients names weights outright.
    std::map<QString, std::vector<double>> named;
    std::map<QString, double> minimums;
    for (const QJsonValue iv : m["ingredients"].toArray()) {
        const QJsonObject i = iv.toObject();
        const QString name = name_of(i["ingredient_name"].toString());
        const double w = i["per_portion_weight_g"].toDouble();
        if (sauce_for(name)) add_sauce(name);
        if (w > 0) named[name].push_back(w);
        if (!minimums.count(name))
            minimums[name] = i["per_portion_minimum_weight_g"].toDouble();
    }

    // It also names what the brand serves. A brand with no dynamic_portion_increases
    // still offers everything listed here; it just has no ramp (step and cap stay 0,
    // so the legacy model holds the portion and adaptive can only trim it). Sauces are
    // left out because the simulator carries them as sauce cups (menu.sauces).
    for (const auto &[name, weights] : named) {
        if (menu.ingredients.count(name) || sauce_for(name)) continue;
        Ingredient ing;
        ing.name = name;
        ing.kind = kind_for(name);
        menu.ingredients[name] = ing;
        order << name;
    }

    // recipes[] carries only a menu_item_id, whose slug holds the dish name.
    // Standalone ids are what a build-your-own order uses; template-scoped ids
    // belong to preconfigured bowls, which are often plated near or above the cap.
    std::vector<WeightedSlug> standalone, templated;
    std::map<QString, double> weight_by_id;
    for (const QJsonValue rv : m["recipes"].toArray()) {
        const QJsonObject r = rv.toObject();
        const double w = r["per_portion_weight_g"].toDouble();
        for (const QJsonValue idv : r["menu_item_ids"].toArray()) {
            const QString id = idv.toString();
            weight_by_id[id] = w;
            if (w <= 0) continue;
            WeightedSlug ws{tokens(slug_of(id)), w};
            (id.contains("mb-pt") ? templated : standalone).push_back(ws);
        }
    }

    // The "no X" removal templates pair a dish slug with a canonical ingredient name.
    std::map<QString, std::vector<QSet<QString>>> alias;
    for (const QJsonValue tv : m["ingredient_templates"].toArray()) {
        const QJsonObject t = tv.toObject();
        for (const QJsonValue rn : t["removal_ingredient_names"].toArray()) {
            QString s = slug_of(t["source_item_id"].toString());
            if (s.startsWith("no-")) s.remove(0, 3);
            if (!s.isEmpty())
                alias[mapped_name(mappings, brand, rn.toString())].push_back(tokens(s));
        }
    }

    auto gather = [](const std::vector<WeightedSlug> &pool, const QSet<QString> &want) {
        std::vector<double> out;
        for (const WeightedSlug &ws : pool)
            if (covers(want, ws.toks)) out.push_back(ws.grams);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };

    for (const QString &name : order) {
        Ingredient &ing = menu.ingredients[name];
        const QSet<QString> want = tokens(name);
        ing.weights_tpl = gather(templated, want);
        ing.per_portion_minimum_weight_g = minimums.count(name) ? minimums[name] : 0.0;

        if (named.count(name)) {
            ing.weights = named[name];
            std::sort(ing.weights.begin(), ing.weights.end());
            ing.weights.erase(std::unique(ing.weights.begin(), ing.weights.end()),
                              ing.weights.end());
            ing.source = WeightSource::Menu;
            continue;
        }
        ing.weights = gather(standalone, want);
        if (!ing.weights.empty()) { ing.source = WeightSource::Slug; continue; }

        if (alias.count(name)) {
            for (const QSet<QString> &a : alias[name]) {
                std::vector<double> hit = gather(standalone, a);
                ing.weights.insert(ing.weights.end(), hit.begin(), hit.end());
            }
            std::sort(ing.weights.begin(), ing.weights.end());
            ing.weights.erase(std::unique(ing.weights.begin(), ing.weights.end()),
                              ing.weights.end());
            if (!ing.weights.empty()) { ing.source = WeightSource::Alias; continue; }
        }
        if (!ing.weights_tpl.empty()) {
            ing.weights = ing.weights_tpl;   // served only inside preconfigured bowls
            ing.source = WeightSource::Template;
        }
    }

    // Second pass: anything still without a weight borrows the modal weight of its
    // class within its family. Must run after the first pass, or the first unresolved
    // ingredient of a class sees no peers and lands on zero.
    for (const QString &name : order) {
        Ingredient &ing = menu.ingredients[name];
        if (!ing.weights.empty()) continue;
        std::map<double, int> tally;
        for (const auto &[other, peer] : menu.ingredients)
            if (peer.kind == ing.kind && !peer.weights.empty()
                && family_of(other) == family_of(name))
                tally[peer.weights.front()]++;
        if (tally.empty()) { ing.source = WeightSource::None; continue; }
        auto best = std::max_element(tally.begin(), tally.end(),
                                     [](auto &a, auto &b) { return a.second < b.second; });
        ing.weights = {best->first};
        ing.source = WeightSource::Class;
    }

    for (const QJsonValue dv : m["dynamic_portion_increases"].toArray()) {
        for (const QJsonValue av : dv["apply_for"].toArray()) {
            const QJsonObject a = av.toObject();
            FloorRule f;
            for (const QJsonValue v : a["ingredient_names"]["must_have"].toArray())
                f.must_have << mapped_name(mappings, brand, v.toString());
            for (const QJsonValue v : a["ingredient_names"]["must_not_have"].toArray())
                f.must_not_have << mapped_name(mappings, brand, v.toString());
            f.op = a["filter_op"].toString() == "ExactlyOne" ? FilterOp::ExactlyOne
                                                             : FilterOp::Any;
            f.minimum_product_weight_g = a["minimum_product_weight_g"].toDouble();
            menu.floors.push_back(f);
        }
    }

    for (const QJsonValue rv : m["product_rules"].toArray()) {
        const QJsonObject r = rv.toObject();
        if (r["type"].toString() != "ProportionalReductionForPortionTarget") continue;
        menu.has_split_rule = true;
        menu.split_rule_items = r["item_ids"].toArray().size();
    }

    // Preconfigured bowls. Names repeat across per-store rows, so keep whichever
    // row carries the most real weights.
    // The brand's own names come first, so a target resolves to what this menu
    // actually serves; other brands' vocabulary is only a fallback.
    // A target slug still spells the menu's own words ("guajillo-cumin-chicken"), so
    // it is matched against the automatic spelling as well as any mapped name, and
    // always lands on the mapped one.
    std::vector<std::pair<QString, QString>> own;   // (spelling to match, name in use)
    for (const auto &[n, ing] : menu.ingredients) own.push_back({n, n});
    for (const auto &[raw, n] : seen) own.push_back({n.automatic, n.name});
    QSet<QString> warned;
    auto resolve = [&](const QString &id, const QString &recipe) -> QString {
        const QSet<QString> st = tokens(slug_of(id));
        QString best, best_match;
        for (const auto &[match, name] : own)
            if (covers(tokens(match), st) && match.size() > best_match.size()) {
                best_match = match;
                best = name;
            }
        if (!best.isEmpty()) return best;
        for (const QString &n : canon)
            if (covers(tokens(n), st) && n.size() > best.size()) best = n;
        // A brand that lists its own ingredients but whose template names another
        // brand's is carrying a stale target (M+R2's "Meat Rice Meat" still points at
        // White Rice); serving it would invent an option the brand does not offer.
        // Sauces are exempt: they legitimately live only inside preconfigured bowls.
        if (!best.isEmpty()) {
            if (named.empty() || sauce_for(best)) return name_of(best);
            if (!warned.contains(recipe + best)) {
                warned.insert(recipe + best);
                issues << QString("%1: %2 targets \"%3\", which this menu does not "
                                    "list; left out")
                                .arg(brand, recipe, best);
            }
            return {};
        }
        const QString tail = id.section(':', -1);
        return is_guid(tail) ? QString() : title_from_slug(tail);
    };

    std::map<QString, Recipe> best_recipe;
    for (const QJsonValue pv : m["product_templates"].toArray()) {
        const QJsonObject p = pv.toObject();
        const QJsonArray targets = p["target_item_ids"].toArray();
        Recipe rec;
        rec.customer_built = targets.isEmpty();
        rec.name = title_from_slug(p["source_item_id"].toString().section(':', 1, 1));
        for (const QJsonValue tv : targets) {
            const QString id = tv.toString();
            QString name = resolve(id, rec.name);
            if (name.isEmpty()) continue;
            // A name this menu already uses stays as it is; anything else (a sauce
            // borrowed from another brand's vocabulary) is a spelling in its own right
            // and can be mapped like one.
            const bool known = std::any_of(seen.begin(), seen.end(),
                                           [&](const auto &e) { return e.second.name == name; });
            if (!known) name = name_of(name);
            double g = weight_by_id.count(id) ? weight_by_id[id] : 0.0;
            if (g > 0) {
                rec.weighted++;
            } else if (const Ingredient *known = menu.find(name)) {
                g = known->full_portion();  // 0 g means "at its normal portion"
            }
            rec.items.push_back({name, g});
        }
        if (rec.items.empty() && !rec.customer_built) continue;
        auto it = best_recipe.find(rec.name);
        if (it == best_recipe.end()
            || (rec.weighted > it->second.weighted && !rec.customer_built))
            best_recipe[rec.name] = rec;
    }

    // Some ingredients appear only inside preconfigured bowls: no ramp settings, so
    // they sit in the bowl at a fixed weight and never step. A recipe's sauces stay on
    // the recipe -- they decide its cups -- but never become bowl ingredients.
    for (const auto &[name, rec] : best_recipe) {
        menu.recipes.push_back(rec);
        for (const RecipeItem &it : rec.items) {
            if (sauce_for(it.name)) {
                add_sauce(it.name);
                continue;
            }
            if (menu.ingredients.count(it.name)) continue;
            Ingredient ing;
            ing.name = it.name;
            ing.kind = kind_for(it.name);
            ing.max_dispense_weight_g = it.grams;
            if (it.grams > 0) ing.weights = {it.grams};
            ing.source = WeightSource::Recipe;
            menu.ingredients[it.name] = ing;
        }
    }
    std::sort(menu.recipes.begin(), menu.recipes.end(),
              [](const Recipe &a, const Recipe &b) { return a.items.size() > b.items.size(); });
    menu.sauces.sort(Qt::CaseInsensitive);

    for (auto &[raw, n] : seen) {
        n.kind = sauce_for(n.name) ? QString("sauce") : to_string(kind_for(n.name));
        n.guessed_kind = is_sauce(n.automatic) ? QString("sauce") : to_string(kind_of(n.automatic));
        menu.names.push_back(n);
    }
    return menu;
}

}  // namespace

QString clean_ingredient_name(const QString &raw) { return clean_name(raw); }

Kind guess_kind(const QString &name) { return kind_of(name); }

bool is_sauce_name(const QString &name) { return is_sauce(name); }

LoadResult load_menus(const QString &dir, const NameMappings *mappings,
                      const PieceHeights *pieces)
{
    LoadResult result;
    QDir d(dir);
    const QStringList files = d.entryList({"*.json"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        result.warnings << QString("no .json menu dumps found in %1").arg(d.absolutePath());
        return result;
    }

    std::vector<QJsonObject> raw;
    QStringList brands;
    for (const QString &f : files) {
        QJsonObject obj = read_dump(d.filePath(f), result.warnings);
        if (obj.isEmpty()) continue;
        raw.push_back(obj);
        QString brand = f;
        brand.chop(5);                    // ".json"
        brands << brand.replace('_', ' ');
    }

    const QStringList canon = canonical_names(raw, brands, mappings);
    for (size_t i = 0; i < raw.size(); ++i)
        result.menus.push_back(build_menu(raw[i], brands[i], canon, mappings, result.issues));

    for (Menu &menu : result.menus)
        for (auto &[name, ing] : menu.ingredients) {
            auto it = pieces ? pieces->find(name) : PieceHeights::const_iterator{};
            if (pieces && it != pieces->end()) {
                ing.piece_height_mm = std::max(0.0, it->second);
            } else if (ing.kind == Kind::Protein) {
                ing.piece_height_mm = kPlaceholderProteinPieceMm;
                ing.piece_height_placeholder = true;
            }
        }
    return result;
}

//
// ############################################################################
// Name mappings
//

const QStringList kMappingKinds = {"base", "protein", "topping", "sauce"};

namespace {

/// One CSV record. Names carry commas ("Guajillo / Cumin Chicken, [cooked]"), so
/// quoted fields and doubled quotes are honoured.
QStringList split_csv(const QString &line)
{
    QStringList out;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out << field;
            field.clear();
        } else {
            field += c;
        }
    }
    out << field;
    return out;
}

QString csv_field(const QString &s)
{
    if (!s.contains(',') && !s.contains('"') && s.trimmed() == s) return s;
    return '"' + QString(s).replace("\"", "\"\"") + '"';
}

}  // namespace

bool load_name_mappings(const QString &path, NameMappings &out, QString *error)
{
    QFile file(path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("could not open %1").arg(path);
        return false;
    }
    NameMappings read;
    int col_brand = -1, col_raw = -1, col_name = -1, col_kind = -1;
    int line_no = 0;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        ++line_no;
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);   // spreadsheet BOM
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList f = split_csv(line);
        if (col_brand < 0) {
            for (int i = 0; i < f.size(); ++i) {
                const QString h = f[i].trimmed().toLower();
                if (h == "brand") col_brand = i;
                else if (h == "ingredient_menu") col_raw = i;
                else if (h == "ingredient_name") col_name = i;
                else if (h == "kind") col_kind = i;
            }
            if (col_brand < 0 || col_raw < 0 || col_name < 0) {
                if (error)
                    *error = QString("%1: the header must name brand, ingredient_menu and "
                                     "ingredient_name").arg(path);
                return false;
            }
            continue;
        }
        const QString brand = f.value(col_brand).trimmed();
        const QString raw = f.value(col_raw);
        if (brand.isEmpty() || raw.trimmed().isEmpty()) continue;
        NameMapping m;
        m.name = f.value(col_name).trimmed();
        m.kind = col_kind >= 0 ? f.value(col_kind).trimmed().toLower() : QString();
        if (!m.kind.isEmpty() && !kMappingKinds.contains(m.kind)) {
            if (error)
                *error = QString("%1, line %2: kind \"%3\" is not one of %4")
                             .arg(path).arg(line_no).arg(m.kind, kMappingKinds.join(", "));
            return false;
        }
        read[{brand, raw}] = m;
    }
    out = std::move(read);
    return true;
}

bool load_piece_heights(const QString &path, PieceHeights &out, QString *error)
{
    QFile file(path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("could not open %1").arg(path);
        return false;
    }
    PieceHeights read;
    int col_name = -1, col_mm = -1, line_no = 0;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        ++line_no;
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList f = split_csv(line);
        if (col_name < 0) {
            for (int i = 0; i < f.size(); ++i) {
                const QString h = f[i].trimmed().toLower();
                if (h == "ingredient") col_name = i;
                else if (h == "piece_height_mm") col_mm = i;
            }
            if (col_name < 0 || col_mm < 0) {
                if (error)
                    *error = QString("%1: the header must name ingredient and "
                                     "piece_height_mm").arg(path);
                return false;
            }
            continue;
        }
        const QString name = f.value(col_name).trimmed();
        if (name.isEmpty() || f.value(col_mm).trimmed().isEmpty()) continue;
        bool ok = false;
        const double mm = f.value(col_mm).trimmed().toDouble(&ok);
        if (!ok || mm < 0) {
            if (error)
                *error = QString("%1, line %2: \"%3\" is not a height in mm")
                             .arg(path).arg(line_no).arg(f.value(col_mm));
            return false;
        }
        read[name] = mm;
    }
    out = std::move(read);
    return true;
}

bool save_piece_heights(const QString &path, const PieceHeights &pieces, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("could not write %1").arg(path);
        return false;
    }
    QTextStream out(&file);
    out << "ingredient,piece_height_mm\n";
    for (const auto &[name, mm] : pieces)
        out << csv_field(name) << ',' << QString::number(mm, 'f', 1) << '\n';
    return true;
}

bool save_name_mappings(const QString &path, const NameMappings &mappings, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("could not write %1").arg(path);
        return false;
    }
    QTextStream out(&file);
    out << "brand,ingredient_menu,ingredient_name,kind\n";
    for (const auto &[key, m] : mappings)
        out << csv_field(key.first) << ',' << csv_field(key.second) << ','
            << csv_field(m.name) << ',' << csv_field(m.kind) << '\n';
    return true;
}

}  // namespace bowlfill
