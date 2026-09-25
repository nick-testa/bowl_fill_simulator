#include "menu_model.hh"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <map>

namespace bowlfill {
namespace {

const QStringList kBases = {"Romaine Base", "Massaged Kale", "White Rice",
                            "Brown Rice and Lentils", "Mexican Rice"};
const QStringList kProteins = {"Chicken Herby", "Chicken Tex-Mex", "Braised Beef",
                               "Beef Shawarma", "Broccoli", "Seared Pork"};

/// Bases split into two families with very different portion sizes; a grain must
/// never borrow a leaf's weight, so the class fallback stays inside the family.
QString family_of(const QString &name)
{
    if (name == "Romaine Base" || name == "Massaged Kale") return "green";
    if (kBases.contains(name)) return "grain";
    return {};
}

Kind kind_of(const QString &name)
{
    if (kBases.contains(name)) return Kind::Base;
    if (kProteins.contains(name)) return Kind::Protein;
    return Kind::Topping;
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
QStringList canonical_names(const std::vector<QJsonObject> &raw)
{
    QSet<QString> names;
    for (const QJsonObject &m : raw) {
        for (const QJsonValue d : m["dynamic_portion_increases"].toArray())
            for (const QJsonValue i : d["ingredients"].toArray())
                names.insert(i["ingredient_name"].toString());
        for (const QJsonValue i : m["ingredients"].toArray())
            names.insert(i["ingredient_name"].toString());
    }
    return QStringList(names.begin(), names.end());
}

Menu build_menu(const QJsonObject &m, const QString &brand, const QStringList &canon)
{
    Menu menu;
    menu.brand = brand;
    menu.version = m["menu_package_version"].toObject()["version_identifier"].toString();

    // Ramp settings, in the order the menu lists them. Farmstand names Broccoli
    // twice with different caps; the first entry wins.
    QStringList order;
    for (const QJsonValue d : m["dynamic_portion_increases"].toArray()) {
        for (const QJsonValue iv : d["ingredients"].toArray()) {
            const QJsonObject i = iv.toObject();
            const QString name = i["ingredient_name"].toString();
            if (menu.ingredients.count(name)) continue;
            Ingredient ing;
            ing.name = name;
            ing.kind = kind_of(name);
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
        const QString name = i["ingredient_name"].toString();
        const double w = i["per_portion_weight_g"].toDouble();
        if (w > 0) named[name].push_back(w);
        if (!minimums.count(name))
            minimums[name] = i["per_portion_minimum_weight_g"].toDouble();
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
            if (!s.isEmpty()) alias[rn.toString()].push_back(tokens(s));
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
                f.must_have << v.toString();
            for (const QJsonValue v : a["ingredient_names"]["must_not_have"].toArray())
                f.must_not_have << v.toString();
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
    auto resolve = [&](const QString &id) -> QString {
        const QSet<QString> st = tokens(slug_of(id));
        QString best;
        for (const QString &n : canon)
            if (covers(tokens(n), st) && n.size() > best.size()) best = n;
        if (!best.isEmpty()) return best;
        const QString tail = id.section(':', -1);
        return is_guid(tail) ? QString() : title_from_slug(tail);
    };

    std::map<QString, Recipe> best_recipe;
    for (const QJsonValue pv : m["product_templates"].toArray()) {
        const QJsonObject p = pv.toObject();
        const QJsonArray targets = p["target_item_ids"].toArray();
        if (targets.isEmpty()) continue;  // the build-your-own entries
        Recipe rec;
        rec.name = title_from_slug(p["source_item_id"].toString().section(':', 1, 1));
        for (const QJsonValue tv : targets) {
            const QString id = tv.toString();
            const QString name = resolve(id);
            if (name.isEmpty()) continue;
            double g = weight_by_id.count(id) ? weight_by_id[id] : 0.0;
            if (g > 0) {
                rec.weighted++;
            } else if (const Ingredient *known = menu.find(name)) {
                g = known->full_portion();  // 0 g means "at its normal portion"
            }
            rec.items.push_back({name, g});
        }
        if (rec.items.empty()) continue;
        auto it = best_recipe.find(rec.name);
        if (it == best_recipe.end() || rec.weighted > it->second.weighted)
            best_recipe[rec.name] = rec;
    }

    // Sauces and the like appear only inside preconfigured bowls: no ramp settings,
    // so they sit in the bowl at a fixed weight and never step.
    for (const auto &[name, rec] : best_recipe) {
        menu.recipes.push_back(rec);
        for (const RecipeItem &it : rec.items) {
            if (menu.ingredients.count(it.name)) continue;
            Ingredient ing;
            ing.name = it.name;
            ing.kind = kind_of(it.name);
            ing.max_dispense_weight_g = it.grams;
            if (it.grams > 0) ing.weights = {it.grams};
            ing.source = WeightSource::Recipe;
            menu.ingredients[it.name] = ing;
        }
    }
    std::sort(menu.recipes.begin(), menu.recipes.end(),
              [](const Recipe &a, const Recipe &b) { return a.items.size() > b.items.size(); });

    return menu;
}

}  // namespace

LoadResult load_menus(const QString &dir)
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

    const QStringList canon = canonical_names(raw);
    for (size_t i = 0; i < raw.size(); ++i)
        result.menus.push_back(build_menu(raw[i], brands[i], canon));

    return result;
}

}  // namespace bowlfill
