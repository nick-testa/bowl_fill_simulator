#include "menu_model.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace bowlfill
{
namespace
{

using nlohmann::json;
namespace fs = std::filesystem;

const std::vector<std::string> kBases = {"Romaine Base",
                                         "Massaged Kale",
                                         "White Rice",
                                         "Brown Rice and Lentils",
                                         "Mexican Rice"};
const std::vector<std::string> kProteins =
    {"Chicken Herby", "Chicken Tex-Mex", "Braised Beef", "Beef Shawarma", "Broccoli", "Seared Pork"};

///
/// The menu never says what kind an ingredient is, and brand refreshes rename
/// everything ("White Rice" becomes "White Jasmine Rice retherm v11"), so beyond the
/// names above the kind is read from words in the name. A prepared salad stays a
/// topping even when it names a meat.
///
const std::set<std::string> kGreenWords = {"romaine", "kale", "lettuce", "greens", "spinach", "arugula"};
const std::set<std::string> kGrainWords = {"rice", "quinoa", "farro", "grains"};
const std::set<std::string> kProteinWords = {"chicken",
                                             "beef",
                                             "pork",
                                             "steak",
                                             "carnitas",
                                             "suadero",
                                             "pastor",
                                             "shawarma",
                                             "barbacoa",
                                             "chorizo",
                                             "lamb",
                                             "turkey",
                                             "tofu",
                                             "salmon",
                                             "shrimp",
                                             "fish"};
const std::set<std::string> kNotProteinWords = {"salad", "broth", "stock"};

/// Portioned into cups rather than dispensed into the bowl. The simulator carries
/// sauce as SauceCups, so these must never be swept or dispensed as toppings. An
/// explicit list from the culinary team, not a keyword guess: Seeds and Cotija go in
/// cups, and nothing about their names says so. Matched as a whole phrase in the
/// lower-cased name, so a brand's spelling ("Cantina Salsa Roja", "Green Salsa
/// Macha") still lands; a per-brand mapping with kind "sauce" or "topping" overrides.
/// A sauce is always one cup: it never ramps, so it never joins the ingredients.
const std::set<std::string> kSauceNames = {"ranch",
                                           "salsa matcha",
                                           "chipotle crema",
                                           "citrus vin",
                                           "salsa macha",
                                           "seeds",
                                           "tzatziki",
                                           "cilantro crema",
                                           "harissa",
                                           "salsa roja",
                                           "sour cream",
                                           "cotija",
                                           "chili crisp",
                                           "miso ginger",
                                           "spicy tahini",
                                           "guacachile salsa",
                                           "salsa cremosa",
                                           // Cupped wherever a brand has said: ORO and M+R2
                                           // map these to sauce, and a new menu should not
                                           // need the mapping before they stop ramping.
                                           "hummus",
                                           "dressing",
                                           "chutney"};

std::set<std::string> tokens(const std::string &text);
std::string clean_name(const std::string &raw);

bool mentions(const std::set<std::string> &toks, const std::set<std::string> &words)
{
    for (const std::string &t : toks)
    {
        if (words.count(t))
        {
            return true;
        }
    }
    return false;
}

/// Bases split into two families with very different portion sizes; a grain must
/// never borrow a leaf's weight, so the class fallback stays inside the family.
std::string family_of(const std::string &name)
{
    if (name == "Romaine Base" || name == "Massaged Kale")
    {
        return "green";
    }
    if (contains(kBases, name))
    {
        return "grain";
    }
    const std::set<std::string> toks = tokens(name);
    if (mentions(toks, kGreenWords))
    {
        return "green";
    }
    if (mentions(toks, kGrainWords))
    {
        return "grain";
    }
    return {};
}

Kind kind_of(const std::string &name)
{
    if (contains(kBases, name))
    {
        return Kind::Base;
    }
    if (contains(kProteins, name))
    {
        return Kind::Protein;
    }
    const std::set<std::string> toks = tokens(name);
    if (mentions(toks, kGreenWords) || mentions(toks, kGrainWords))
    {
        return Kind::Base;
    }
    if (mentions(toks, kProteinWords) && !mentions(toks, kNotProteinWords))
    {
        return Kind::Protein;
    }
    return Kind::Topping;
}

/// Checked after kind_of, so a "Ranch Chicken" would still be a protein.
bool is_sauce(const std::string &name)
{
    if (kind_of(name) != Kind::Topping)
    {
        return false;
    }
    const std::string padded = " " + to_lower(clean_name(name)) + " ";
    for (const std::string &sauce : kSauceNames)
    {
        if (padded.find(" " + sauce + " ") != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

///
/// The name an ingredient is offered under. Menus spell one ingredient several ways
/// across stores and recipe revisions ("White Jasmine Rice Retherm", "White Jasmine
/// Rice, retherm v1.1", "White Jasmine Rice retherm v11"); prep state, portioning and
/// version words say how it was made, not what it is, so they are dropped and the
/// spellings collapse onto one option.
///
std::string clean_name(const std::string &raw)
{
    // Regex replacement is slow next to everything else the loader does, and the same
    // few hundred spellings are cleaned over and over.
    static std::mutex lock;
    static std::map<std::string, std::string> cache;
    {
        const std::lock_guard<std::mutex> hold(lock);
        if (auto it = cache.find(raw); it != cache.end())
        {
            return it->second;
        }
    }

    static const std::regex brackets(R"(\[[^\]]*\]|\([^)]*\))");
    static const std::regex prep(R"(\b(retherm|rethermed|cooked|portion|portioned|v\d+(\.\d+)*)\b)", std::regex::icase);
    static const std::regex punct(R"([^\w\s'&+-])");
    static const std::regex space(R"(\s+)");
    std::string s = std::regex_replace(raw, brackets, "");
    s = std::regex_replace(s, prep, "");
    s = std::regex_replace(s, punct, " ");
    s = trim(std::regex_replace(s, space, " "));
    if (s.empty())
    {
        s = trim(raw);
    }
    const std::lock_guard<std::mutex> hold(lock);
    cache.emplace(raw, s);
    return s;
}

const std::set<std::string> kStopWords =
    {"base", "and", "a", "the", "plain", "single", "portion", "mb", "m", "no", "bowl"};

/// Runs of letters and digits.
std::vector<std::string> words_of(const std::string &text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum)
        {
            cur += c;
        }
        else if (!cur.empty())
        {
            out.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty())
    {
        out.push_back(std::move(cur));
    }
    return out;
}

std::set<std::string> tokens(const std::string &text)
{
    std::set<std::string> out;
    for (const std::string &part : words_of(to_lower(text)))
    {
        if (!kStopWords.count(part))
        {
            out.insert(part);
        }
    }
    return out;
}

bool is_guid(const std::string &s)
{
    static const std::regex re("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return std::regex_match(s, re);
}

/// What follows the last `separator`, or all of `s` when there is none.
std::string last_section(const std::string &s, char separator)
{
    const size_t at = s.rfind(separator);
    return at == std::string::npos ? s : s.substr(at + 1);
}

///
/// Reduce a menu_item_id to the words naming the ingredient or dish. Three shapes
/// occur and the name sits in a different place in each:
///   mb:white-rice-a-<32hex>-cddd:<guid>                        -> leading words
///   mb-pt:mb:<template>:<guid>:mexican-rice                    -> trailing segment
///   mb:mb-pt-mb-<template>-<guid>-brown-rice-and-lentils:<guid> -> after an inline guid
///
std::string slug_of(const std::string &menu_item_id)
{
    std::string body = menu_item_id.starts_with("mb:") ? menu_item_id.substr(3) : menu_item_id;

    std::string keep;
    const size_t last = body.rfind(':');
    if (last != std::string::npos)
    {
        const std::string tail = body.substr(last + 1);
        if (is_guid(tail))
        {
            body = body.substr(0, last);
        }
        else
        {
            keep = tail;
            body = body.substr(0, last);
        }
    }

    static const std::regex long_hash("-a-[0-9a-f]{20,}.*$");
    body = std::regex_replace(body, long_hash, "");
    static const std::regex inline_guid("-?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    body = std::regex_replace(body, inline_guid, "-");
    static const std::regex short_prefix("^m-[a-z]{2,4}-");
    body = std::regex_replace(body, short_prefix, "");

    return keep.empty() ? body : body + "-" + keep;
}

std::string title_from_slug(const std::string &s)
{
    std::vector<std::string> words = words_of(s);
    for (std::string &w : words)
    {
        if (w[0] >= 'a' && w[0] <= 'z')
        {
            w[0] = static_cast<char>(w[0] - 'a' + 'A');
        }
    }
    return join(words, " ");
}

/// True when every token of `needle` appears in `haystack`, i.e. the name is fully
/// spelled out inside the slug.
bool covers(const std::set<std::string> &needle, const std::set<std::string> &haystack)
{
    if (needle.empty())
    {
        return false;
    }
    for (const std::string &t : needle)
    {
        if (!haystack.count(t))
        {
            return false;
        }
    }
    return true;
}

struct WeightedSlug
{
    std::set<std::string> toks;
    double grams = 0.0;
};

//
// Reading the JSON the way the menus are written: a missing key, or one holding the
// wrong type, reads as empty or zero rather than failing the whole menu.
//

const json &field(const json &j, const char *key)
{
    static const json null;
    if (!j.is_object())
    {
        return null;
    }
    const auto it = j.find(key);
    return it == j.end() ? null : *it;
}

const json &array(const json &j)
{
    static const json empty = json::array();
    return j.is_array() ? j : empty;
}

const json &array(const json &j, const char *key)
{
    return array(field(j, key));
}

std::string text(const json &j)
{
    return j.is_string() ? j.get<std::string>() : std::string();
}

std::string text(const json &j, const char *key)
{
    return text(field(j, key));
}

double number(const json &j, const char *key)
{
    const json &v = field(j, key);
    return v.is_number() ? v.get<double>() : 0.0;
}

json read_dump(const std::string &path, std::vector<std::string> &warnings)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        warnings.push_back(std::format("could not open {}", path));
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    // The tool writes "ID: <env>\nMAPPINGS:\n" ahead of the JSON body.
    const size_t marker = text.find("MAPPINGS:");
    const std::string body = marker != std::string::npos ? text.substr(marker + 9) : text;

    try
    {
        json doc = json::parse(body);
        return doc.is_object() ? doc : json{};
    }
    catch (const json::parse_error &e)
    {
        warnings.push_back(std::format("{}: {}", fs::path(path).filename().string(), e.what()));
        return {};
    }
}

} // namespace

std::string to_string(Kind kind)
{
    switch (kind)
    {
    case Kind::Base:
        return "base";
    case Kind::Protein:
        return "protein";
    case Kind::Topping:
        return "topping";
    }
    return {};
}

std::string to_string(WeightSource source)
{
    switch (source)
    {
    case WeightSource::Menu:
        return "menu";
    case WeightSource::Slug:
        return "slug";
    case WeightSource::Alias:
        return "alias";
    case WeightSource::Template:
        return "template";
    case WeightSource::Class:
        return "inferred";
    case WeightSource::Recipe:
        return "recipe";
    case WeightSource::None:
        return "none";
    }
    return {};
}

bool FloorRule::matches(const std::vector<std::string> &bowl) const
{
    for (const std::string &n : must_not_have)
    {
        if (contains(bowl, n))
        {
            return false;
        }
    }
    if (must_have.empty())
    {
        return true;
    }
    int count = 0;
    for (const std::string &n : bowl)
    {
        if (contains(must_have, n))
        {
            ++count;
        }
    }
    return op == FilterOp::ExactlyOne ? count == 1 : count > 0;
}

const Ingredient *Menu::find(const std::string &name) const
{
    auto it = ingredients.find(name);
    return it == ingredients.end() ? nullptr : &it->second;
}

std::vector<std::string> Menu::sauces_in(const Recipe &recipe) const
{
    std::vector<std::string> out;
    for (const RecipeItem &ri : recipe.items)
    {
        if (is_sauce(ri.name))
        {
            append_unique(out, ri.name);
        }
    }
    return out;
}

std::vector<std::string> Menu::names_of(Kind kind) const
{
    std::vector<std::string> out;
    if (!order.empty())
    {
        for (const std::string &name : order)
        {
            if (const Ingredient *ing = find(name); ing && ing->kind == kind)
            {
                out.push_back(name);
            }
        }
        return out;
    }
    for (const auto &[name, ing] : ingredients)
    {
        if (ing.kind == kind)
        {
            out.push_back(name);
        }
    }
    return out;
}

const FloorRule *Menu::floor_for(const std::vector<std::string> &bowl) const
{
    for (const FloorRule &f : floors)
    {
        if (f.matches(bowl))
        {
            return &f;
        }
    }
    return nullptr;
}

//
// ############################################################################
//

namespace
{

///
/// Every ingredient name any brand configures. Meat + Rice has no
/// dynamic_portion_increases at all, so its template targets can only be named by
/// borrowing the vocabulary of the other brands.
///
/// The name a brand's menu spelling is offered under: the person's mapping if there
/// is one, otherwise the automatic clean-up.
std::string mapped_name(const NameMappings *mappings, const std::string &brand, const std::string &raw)
{
    if (mappings)
    {
        auto it = mappings->find({brand, raw});
        if (it != mappings->end() && !trim(it->second.name).empty())
        {
            return trim(it->second.name);
        }
    }
    return clean_name(raw);
}

std::vector<std::string> canonical_names(const std::vector<json> &raw,
                                         const std::vector<std::string> &brands,
                                         const NameMappings *mappings)
{
    std::set<std::string> names;
    for (size_t b = 0; b < raw.size(); ++b)
    {
        const json &m = raw[b];
        for (const json &d : array(m, "dynamic_portion_increases"))
        {
            for (const json &i : array(d, "ingredients"))
            {
                names.insert(mapped_name(mappings, brands[b], text(i, "ingredient_name")));
            }
        }
        for (const json &i : array(m, "ingredients"))
        {
            names.insert(mapped_name(mappings, brands[b], text(i, "ingredient_name")));
        }
    }
    return {names.begin(), names.end()};
}

Menu build_menu(const json &m,
                const std::string &brand,
                const std::vector<std::string> &canon,
                const NameMappings *mappings,
                std::vector<std::string> &issues)
{
    Menu menu;
    menu.brand = brand;

    // ---- names: every spelling goes through here ---------------------------
    // Kind overrides are keyed by the name in use, so two spellings mapped onto one
    // name share whichever kind was set for either.
    std::map<std::string, std::string> kind_override;
    if (mappings)
    {
        for (const auto &[key, mapping] : *mappings)
        {
            if (key.first == brand && !mapping.kind.empty())
            {
                kind_override[mapped_name(mappings, brand, key.second)] = mapping.kind;
            }
        }
    }

    auto kind_for = [&](const std::string &name) {
        auto it = kind_override.find(name);
        if (it == kind_override.end())
        {
            return kind_of(name);
        }
        if (it->second == "base")
        {
            return Kind::Base;
        }
        if (it->second == "protein")
        {
            return Kind::Protein;
        }
        return Kind::Topping; // topping, and sauce (which never reaches a pool)
    };
    auto sauce_for = [&](const std::string &name) {
        auto it = kind_override.find(name);
        return it == kind_override.end() ? is_sauce(name) : it->second == "sauce";
    };
    auto add_sauce = [&](const std::string &name) { append_unique(menu.sauces, name); };

    std::map<std::string, MenuName> seen; // by raw spelling
    auto name_of = [&](const std::string &raw) {
        const std::string name = mapped_name(mappings, brand, raw);
        if (!seen.count(raw))
        {
            MenuName n;
            n.raw = raw;
            n.automatic = clean_name(raw);
            n.name = name;
            seen[raw] = n;
        }
        return name;
    };
    menu.version = text(field(m, "menu_package_version"), "version_identifier");

    // Ramp settings, in the order the menu lists them. Farmstand names Broccoli
    // twice with different caps; the first entry wins.
    std::vector<std::string> order;
    for (const json &d : array(m, "dynamic_portion_increases"))
    {
        for (const json &i : array(d, "ingredients"))
        {
            const std::string name = name_of(text(i, "ingredient_name"));
            if (sauce_for(name))
            {
                add_sauce(name);
                continue;
            }
            if (menu.ingredients.count(name))
            {
                continue;
            }
            Ingredient ing;
            ing.name = name;
            ing.kind = kind_for(name);
            ing.step_increment_g = number(i, "step_increment_g");
            ing.max_dispense_weight_g = number(i, "max_dispense_weight_g");
            menu.ingredients[name] = ing;
            order.push_back(name);
        }
    }

    // mappings.ingredients names weights outright.
    std::map<std::string, std::vector<double>> named;
    std::map<std::string, double> minimums;
    for (const json &i : array(m, "ingredients"))
    {
        const std::string name = name_of(text(i, "ingredient_name"));
        const double w = number(i, "per_portion_weight_g");
        if (sauce_for(name))
        {
            add_sauce(name);
        }
        if (w > 0)
        {
            named[name].push_back(w);
        }
        if (!minimums.count(name))
        {
            minimums[name] = number(i, "per_portion_minimum_weight_g");
        }
    }

    // It also names what the brand serves. A brand with no dynamic_portion_increases
    // still offers everything listed here; it just has no ramp (step and cap stay 0,
    // so the legacy model holds the portion and adaptive can only trim it). Sauces are
    // left out because the simulator carries them as sauce cups (menu.sauces).
    for (const auto &[name, weights] : named)
    {
        if (menu.ingredients.count(name) || sauce_for(name))
        {
            continue;
        }
        Ingredient ing;
        ing.name = name;
        ing.kind = kind_for(name);
        menu.ingredients[name] = ing;
        order.push_back(name);
    }

    // recipes[] carries only a menu_item_id, whose slug holds the dish name.
    // Standalone ids are what a build-your-own order uses; template-scoped ids
    // belong to preconfigured bowls, which are often plated near or above the cap.
    std::vector<WeightedSlug> standalone, templated;
    std::map<std::string, double> weight_by_id;
    for (const json &r : array(m, "recipes"))
    {
        const double w = number(r, "per_portion_weight_g");
        for (const json &idv : array(r, "menu_item_ids"))
        {
            const std::string id = text(idv);
            weight_by_id[id] = w;
            if (w <= 0)
            {
                continue;
            }
            WeightedSlug ws{tokens(slug_of(id)), w};
            (id.find("mb-pt") != std::string::npos ? templated : standalone).push_back(ws);
        }
    }

    // The "no X" removal templates pair a dish slug with a canonical ingredient name.
    std::map<std::string, std::vector<std::set<std::string>>> alias;
    for (const json &t : array(m, "ingredient_templates"))
    {
        for (const json &rn : array(t, "removal_ingredient_names"))
        {
            std::string s = slug_of(text(t, "source_item_id"));
            if (s.starts_with("no-"))
            {
                s.erase(0, 3);
            }
            if (!s.empty())
            {
                alias[mapped_name(mappings, brand, text(rn))].push_back(tokens(s));
            }
        }
    }

    auto gather = [](const std::vector<WeightedSlug> &pool, const std::set<std::string> &want) {
        std::vector<double> out;
        for (const WeightedSlug &ws : pool)
        {
            if (covers(want, ws.toks))
            {
                out.push_back(ws.grams);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };

    for (const std::string &name : order)
    {
        Ingredient &ing = menu.ingredients[name];
        const std::set<std::string> want = tokens(name);
        ing.weights_tpl = gather(templated, want);
        ing.per_portion_minimum_weight_g = minimums.count(name) ? minimums[name] : 0.0;

        if (named.count(name))
        {
            ing.weights = named[name];
            std::sort(ing.weights.begin(), ing.weights.end());
            ing.weights.erase(std::unique(ing.weights.begin(), ing.weights.end()), ing.weights.end());
            ing.source = WeightSource::Menu;
            continue;
        }
        ing.weights = gather(standalone, want);
        if (!ing.weights.empty())
        {
            ing.source = WeightSource::Slug;
            continue;
        }

        if (alias.count(name))
        {
            for (const std::set<std::string> &a : alias[name])
            {
                std::vector<double> hit = gather(standalone, a);
                ing.weights.insert(ing.weights.end(), hit.begin(), hit.end());
            }
            std::sort(ing.weights.begin(), ing.weights.end());
            ing.weights.erase(std::unique(ing.weights.begin(), ing.weights.end()), ing.weights.end());
            if (!ing.weights.empty())
            {
                ing.source = WeightSource::Alias;
                continue;
            }
        }
        if (!ing.weights_tpl.empty())
        {
            ing.weights = ing.weights_tpl; // served only inside preconfigured bowls
            ing.source = WeightSource::Template;
        }
    }

    // Second pass: anything still without a weight borrows the modal weight of its
    // class within its family. Must run after the first pass, or the first unresolved
    // ingredient of a class sees no peers and lands on zero.
    for (const std::string &name : order)
    {
        Ingredient &ing = menu.ingredients[name];
        if (!ing.weights.empty())
        {
            continue;
        }
        std::map<double, int> tally;
        for (const auto &[other, peer] : menu.ingredients)
        {
            if (peer.kind == ing.kind && !peer.weights.empty() && family_of(other) == family_of(name))
            {
                tally[peer.weights.front()]++;
            }
        }
        if (tally.empty())
        {
            ing.source = WeightSource::None;
            continue;
        }
        auto best = std::max_element(tally.begin(), tally.end(), [](auto &a, auto &b) { return a.second < b.second; });
        ing.weights = {best->first};
        ing.source = WeightSource::Class;
    }

    for (const json &d : array(m, "dynamic_portion_increases"))
    {
        for (const json &a : array(d, "apply_for"))
        {
            FloorRule f;
            const json &names = field(a, "ingredient_names");
            for (const json &v : array(names, "must_have"))
            {
                f.must_have.push_back(mapped_name(mappings, brand, text(v)));
            }
            for (const json &v : array(names, "must_not_have"))
            {
                f.must_not_have.push_back(mapped_name(mappings, brand, text(v)));
            }
            f.op = text(a, "filter_op") == "ExactlyOne" ? FilterOp::ExactlyOne : FilterOp::Any;
            f.minimum_product_weight_g = number(a, "minimum_product_weight_g");
            menu.floors.push_back(f);
        }
    }

    for (const json &r : array(m, "product_rules"))
    {
        if (text(r, "type") != "ProportionalReductionForPortionTarget")
        {
            continue;
        }
        menu.has_split_rule = true;
        menu.split_rule_items = static_cast<int>(array(r, "item_ids").size());
    }

    // Preconfigured bowls. Names repeat across per-store rows, so keep whichever
    // row carries the most real weights.
    // The brand's own names come first, so a target resolves to what this menu
    // actually serves; other brands' vocabulary is only a fallback.
    // A target slug still spells the menu's own words ("guajillo-cumin-chicken"), so
    // it is matched against the automatic spelling as well as any mapped name, and
    // always lands on the mapped one.
    std::vector<std::pair<std::string, std::string>> own; // (spelling to match, name in use)
    for (const auto &[n, ing] : menu.ingredients)
    {
        own.push_back({n, n});
    }
    for (const auto &[raw, n] : seen)
    {
        own.push_back({n.automatic, n.name});
    }
    std::set<std::string> warned;
    auto resolve = [&](const std::string &id, const std::string &recipe) -> std::string {
        const std::set<std::string> st = tokens(slug_of(id));
        std::string best, best_match;
        for (const auto &[match, name] : own)
        {
            if (covers(tokens(match), st) && match.size() > best_match.size())
            {
                best_match = match;
                best = name;
            }
        }
        if (!best.empty())
        {
            return best;
        }
        for (const std::string &n : canon)
        {
            if (covers(tokens(n), st) && n.size() > best.size())
            {
                best = n;
            }
        }
        // A brand that lists its own ingredients but whose template names another
        // brand's is carrying a stale target (M+R2's "Meat Rice Meat" still points at
        // White Rice); serving it would invent an option the brand does not offer.
        // Sauces are exempt: they legitimately live only inside preconfigured bowls.
        if (!best.empty())
        {
            if (named.empty() || sauce_for(best))
            {
                return name_of(best);
            }
            if (!warned.count(recipe + best))
            {
                warned.insert(recipe + best);
                issues.push_back(
                    std::format("{}: {} targets \"{}\", which this menu does not list; left out", brand, recipe, best));
            }
            return {};
        }
        const std::string tail = last_section(id, ':');
        return is_guid(tail) ? std::string() : title_from_slug(tail);
    };

    std::map<std::string, Recipe> best_recipe;
    for (const json &p : array(m, "product_templates"))
    {
        const json &targets = array(p, "target_item_ids");
        Recipe rec;
        rec.customer_built = targets.empty();
        const std::vector<std::string> source = split(text(p, "source_item_id"), ':');
        rec.name = title_from_slug(source.size() > 1 ? source[1] : std::string());
        for (const json &tv : targets)
        {
            const std::string id = text(tv);
            std::string name = resolve(id, rec.name);
            if (name.empty())
            {
                continue;
            }
            // A name this menu already uses stays as it is; anything else (a sauce
            // borrowed from another brand's vocabulary) is a spelling in its own right
            // and can be mapped like one.
            const bool known =
                std::any_of(seen.begin(), seen.end(), [&](const auto &e) { return e.second.name == name; });
            if (!known)
            {
                name = name_of(name);
            }
            double g = weight_by_id.count(id) ? weight_by_id[id] : 0.0;
            if (g > 0)
            {
                rec.weighted++;
            }
            else if (const Ingredient *known = menu.find(name))
            {
                g = known->full_portion(); // 0 g means "at its normal portion"
            }
            rec.items.push_back({name, g});
        }
        if (rec.items.empty() && !rec.customer_built)
        {
            continue;
        }
        auto it = best_recipe.find(rec.name);
        if (it == best_recipe.end() || (rec.weighted > it->second.weighted && !rec.customer_built))
        {
            best_recipe[rec.name] = rec;
        }
    }

    // Some ingredients appear only inside preconfigured bowls: no ramp settings, so
    // they sit in the bowl at a fixed weight and never step. A recipe's sauces stay on
    // the recipe -- they decide its cups -- but never become bowl ingredients.
    for (const auto &[name, rec] : best_recipe)
    {
        menu.recipes.push_back(rec);
        for (const RecipeItem &it : rec.items)
        {
            if (sauce_for(it.name))
            {
                add_sauce(it.name);
                continue;
            }
            if (menu.ingredients.count(it.name))
            {
                continue;
            }
            Ingredient ing;
            ing.name = it.name;
            ing.kind = kind_for(it.name);
            ing.max_dispense_weight_g = it.grams;
            if (it.grams > 0)
            {
                ing.weights = {it.grams};
            }
            ing.source = WeightSource::Recipe;
            menu.ingredients[it.name] = ing;
        }
    }
    std::sort(menu.recipes.begin(), menu.recipes.end(), [](const Recipe &a, const Recipe &b) {
        return a.items.size() > b.items.size();
    });
    sort_case_insensitive(menu.sauces);

    for (auto &[raw, n] : seen)
    {
        n.kind = sauce_for(n.name) ? std::string("sauce") : to_string(kind_for(n.name));
        n.guessed_kind = is_sauce(n.automatic) ? std::string("sauce") : to_string(kind_of(n.automatic));
        menu.names.push_back(n);
    }
    return menu;
}

/// Regular, visible files in `dir` whose extension is `ext` in any case, sorted by
/// name.
std::vector<std::string> files_with_extension(const std::string &dir, const std::string &ext)
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
        if (iequals(e.path().extension().string(), ext))
        {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::string clean_ingredient_name(const std::string &raw)
{
    return clean_name(raw);
}

Kind guess_kind(const std::string &name)
{
    return kind_of(name);
}

bool is_sauce_name(const std::string &name)
{
    return is_sauce(name);
}

LoadResult load_menus(const std::string &dir, const NameMappings *mappings, const PieceHeights *pieces)
{
    LoadResult result;
    const std::vector<std::string> files = files_with_extension(dir, ".json");
    if (files.empty())
    {
        std::error_code ec;
        result.warnings.push_back(
            std::format("no .json menu dumps found in {}", fs::absolute(dir, ec).lexically_normal().string()));
        return result;
    }

    std::vector<json> raw;
    std::vector<std::string> brands;
    for (const std::string &f : files)
    {
        json obj = read_dump((fs::path(dir) / f).string(), result.warnings);
        if (!obj.is_object() || obj.empty())
        {
            continue;
        }
        raw.push_back(std::move(obj));
        std::string brand = f.substr(0, f.size() - 5); // ".json"
        std::replace(brand.begin(), brand.end(), '_', ' ');
        brands.push_back(brand);
    }

    const std::vector<std::string> canon = canonical_names(raw, brands, mappings);
    for (size_t i = 0; i < raw.size(); ++i)
    {
        result.menus.push_back(build_menu(raw[i], brands[i], canon, mappings, result.issues));
    }

    for (Menu &menu : result.menus)
    {
        for (auto &[name, ing] : menu.ingredients)
        {
            auto it = pieces ? pieces->find(name) : PieceHeights::const_iterator{};
            if (pieces && it != pieces->end())
            {
                ing.piece_height_mm = std::max(0.0, it->second);
            }
            else if (ing.kind == Kind::Protein)
            {
                ing.piece_height_mm = kPlaceholderProteinPieceMm;
                ing.piece_height_placeholder = true;
            }
        }
    }
    return result;
}

//
// ############################################################################
// Name mappings
//

const std::vector<std::string> kMappingKinds = {"base", "protein", "topping", "sauce"};

namespace
{

/// Field `i` of a record, or empty past its end.
std::string field_at(const std::vector<std::string> &fields, int i)
{
    return i >= 0 && i < static_cast<int>(fields.size()) ? fields[i] : std::string();
}

std::string csv_field(const std::string &s)
{
    if (s.find(',') == std::string::npos && s.find('"') == std::string::npos && trim(s) == s)
    {
        return s;
    }
    return '"' + replace_all(s, "\"", "\"\"") + '"';
}

} // namespace

bool load_name_mappings(const std::string &path, NameMappings &out, std::string *error)
{
    std::error_code ec;
    if (!fs::exists(path, ec))
    {
        return true;
    }
    std::vector<std::string> lines;
    if (!read_lines(path, lines))
    {
        if (error)
        {
            *error = std::format("could not open {}", path);
        }
        return false;
    }
    NameMappings read;
    int col_brand = -1, col_raw = -1, col_name = -1, col_kind = -1;
    int line_no = 0;
    for (const std::string &raw_line : lines)
    {
        ++line_no;
        const std::string line = trim(raw_line);
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const std::vector<std::string> f = split_csv(line);
        if (col_brand < 0)
        {
            for (int i = 0; i < static_cast<int>(f.size()); ++i)
            {
                const std::string h = to_lower(trim(f[i]));
                if (h == "brand")
                {
                    col_brand = i;
                }
                else if (h == "ingredient_menu")
                {
                    col_raw = i;
                }
                else if (h == "ingredient_name")
                {
                    col_name = i;
                }
                else if (h == "kind")
                {
                    col_kind = i;
                }
            }
            if (col_brand < 0 || col_raw < 0 || col_name < 0)
            {
                if (error)
                {
                    *error = std::format("{}: the header must name brand, ingredient_menu and "
                                         "ingredient_name",
                                         path);
                }
                return false;
            }
            continue;
        }
        const std::string brand = trim(field_at(f, col_brand));
        const std::string raw = field_at(f, col_raw);
        if (brand.empty() || trim(raw).empty())
        {
            continue;
        }
        NameMapping m;
        m.name = trim(field_at(f, col_name));
        m.kind = col_kind >= 0 ? to_lower(trim(field_at(f, col_kind))) : std::string();
        if (!m.kind.empty() && !contains(kMappingKinds, m.kind))
        {
            if (error)
            {
                *error = std::format(
                    "{}, line {}: kind \"{}\" is not one of {}", path, line_no, m.kind, join(kMappingKinds, ", "));
            }
            return false;
        }
        read[{brand, raw}] = m;
    }
    out = std::move(read);
    return true;
}

namespace
{

/// Reads `ingredient,<column>` rows of non-negative numbers; a missing file is an
/// empty table. `what` names a value in error messages ("a height in mm").
bool load_named_numbers(const std::string &path,
                        const std::string &column,
                        const std::string &what,
                        std::map<std::string, double> &out,
                        std::string *error)
{
    std::error_code ec;
    if (!fs::exists(path, ec))
    {
        return true;
    }
    std::vector<std::string> lines;
    if (!read_lines(path, lines))
    {
        if (error)
        {
            *error = std::format("could not open {}", path);
        }
        return false;
    }
    std::map<std::string, double> read;
    int col_name = -1, col_value = -1, line_no = 0;
    for (const std::string &raw_line : lines)
    {
        ++line_no;
        const std::string line = trim(raw_line);
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const std::vector<std::string> f = split_csv(line);
        if (col_name < 0)
        {
            for (int i = 0; i < static_cast<int>(f.size()); ++i)
            {
                const std::string h = to_lower(trim(f[i]));
                if (h == "ingredient")
                {
                    col_name = i;
                }
                else if (h == column)
                {
                    col_value = i;
                }
            }
            if (col_name < 0 || col_value < 0)
            {
                if (error)
                {
                    *error = std::format("{}: the header must name ingredient and {}", path, column);
                }
                return false;
            }
            continue;
        }
        const std::string name = trim(field_at(f, col_name));
        if (name.empty() || trim(field_at(f, col_value)).empty())
        {
            continue;
        }
        const std::optional<double> v = parse_double(field_at(f, col_value));
        if (!v || *v < 0)
        {
            if (error)
            {
                *error = std::format("{}, line {}: \"{}\" is not {}", path, line_no, field_at(f, col_value), what);
            }
            return false;
        }
        read[name] = *v;
    }
    out = std::move(read);
    return true;
}

bool save_named_numbers(const std::string &path,
                        const std::string &column,
                        const std::map<std::string, double> &values,
                        std::string *error)
{
    std::string out = "ingredient," + column + "\n";
    for (const auto &[name, v] : values)
    {
        out += csv_field(name) + ',' + fixed(v, 1) + '\n';
    }
    if (!write_text(path, out))
    {
        if (error)
        {
            *error = std::format("could not write {}", path);
        }
        return false;
    }
    return true;
}

} // namespace

bool load_piece_heights(const std::string &path, PieceHeights &out, std::string *error)
{
    return load_named_numbers(path, "piece_height_mm", "a height in mm", out, error);
}

bool save_piece_heights(const std::string &path, const PieceHeights &pieces, std::string *error)
{
    return save_named_numbers(path, "piece_height_mm", pieces, error);
}

bool load_mound_slopes(const std::string &path, MoundSlopes &out, std::string *error)
{
    return load_named_numbers(path, "mound_slope_deg", "a slope in degrees", out, error);
}

bool save_mound_slopes(const std::string &path, const MoundSlopes &slopes, std::string *error)
{
    return save_named_numbers(path, "mound_slope_deg", slopes, error);
}

bool load_roll_off(const std::string &path, RollOffAngles &out, std::string *error)
{
    return load_named_numbers(path, "roll_off_deg", "an angle in degrees", out, error);
}

bool save_roll_off(const std::string &path, const RollOffAngles &angles, std::string *error)
{
    return save_named_numbers(path, "roll_off_deg", angles, error);
}

bool save_name_mappings(const std::string &path, const NameMappings &mappings, std::string *error)
{
    std::string out = "brand,ingredient_menu,ingredient_name,kind\n";
    for (const auto &[key, m] : mappings)
    {
        out += csv_field(key.first) + ',' + csv_field(key.second) + ',' + csv_field(m.name) + ',' + csv_field(m.kind) +
               '\n';
    }
    if (!write_text(path, out))
    {
        if (error)
        {
            *error = std::format("could not write {}", path);
        }
        return false;
    }
    return true;
}

} // namespace bowlfill
