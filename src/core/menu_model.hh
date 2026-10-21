#pragma once

#include "text.hh"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bowlfill
{

enum class Kind
{
    Base,
    Protein,
    Topping
};

std::string to_string(Kind kind);

/// The lab37 Catalog category_name that gives Volume Fill this kind: Base, Protein or Toppings.
std::string catalog_category(Kind kind);

///
/// How an ingredient's portion weight was established. Anything below Slug is a
/// guess and is surfaced to the user rather than passed off as configuration.
///
enum class WeightSource
{
    Menu,     ///< named outright in mappings.ingredients
    Slug,     ///< matched by name tokens against a standalone menu_item_id
    Alias,    ///< matched via a "no X" removal template's dish slug
    Template, ///< only ever served inside a preconfigured bowl
    Class,    ///< borrowed from the modal weight of its class and family
    Recipe,   ///< appears only in a recipe, with no ramp settings of its own
    None,
};

std::string to_string(WeightSource source);

struct Ingredient
{
    std::string name;
    Kind kind = Kind::Topping;
    double step_increment_g = 0.0;
    double max_dispense_weight_g = 0.0;
    double per_portion_minimum_weight_g = 0.0;
    std::vector<double> weights;     ///< standalone (build-your-own) portions, ascending
    std::vector<double> weights_tpl; ///< portions seen only inside preconfigured bowls
    WeightSource source = WeightSource::None;

    /// Height of one piece, for chunky ingredients; 0 means it smears. Comes from
    /// data/ingredient_pieces.csv, or a placeholder for proteins until measured.
    double piece_height_mm = 0.0;
    bool piece_height_placeholder = false;

    /// The largest configured standalone portion: what a build-your-own bowl starts
    /// its ramp from, and the worst case for volume.
    double full_portion() const
    {
        return weights.empty() ? 0.0 : weights.back();
    }
};

enum class FilterOp
{
    ExactlyOne,
    Any
};

///
/// One entry of dynamic_portion_increases[].apply_for. Mirrors
/// DynamicPortionAlgorithmFilter in lab37/orders/menu_mappings.rbuf.
///
struct FloorRule
{
    std::vector<std::string> must_have;
    std::vector<std::string> must_not_have;
    FilterOp op = FilterOp::Any;
    double minimum_product_weight_g = 0.0;

    bool matches(const std::vector<std::string> &bowl) const;
};

struct RecipeItem
{
    std::string name;
    double grams = 0.0;
};

struct Recipe
{
    std::string name;
    std::vector<RecipeItem> items;
    int weighted = 0; ///< how many items carried a real weight in the menu
    /// A template with no target_item_ids: the customer picks every ingredient.
    /// Not offered in the recipe picker, but still audited.
    bool customer_built = false;

    int unweighted() const
    {
        return static_cast<int>(items.size()) - weighted;
    }
};

///
/// One spelling of an ingredient as a menu uses it, and what the loader made of it.
/// The mapping editor lists these; they are what a NameMapping is keyed on.
///
struct MenuName
{
    std::string raw;          ///< as the menu JSON spells it
    std::string automatic;    ///< the name the loader derives with no mapping applied
    std::string name;         ///< the name in use, after any mapping
    std::string kind;         ///< "base", "protein", "topping" or "sauce", after any mapping
    std::string guessed_kind; ///< the kind the loader guesses from the automatic name
};

struct Menu
{
    std::string brand;
    std::string version;
    std::map<std::string, Ingredient> ingredients;
    std::vector<FloorRule> floors;
    std::vector<Recipe> recipes;
    bool has_split_rule = false;
    int split_rule_items = 0;
    std::vector<MenuName> names; ///< every spelling the menu used, sorted by raw
    /// What the brand serves in sauce cups, sorted. Never in `ingredients`: a sauce
    /// takes a cup's room in the bowl, not a dispensed weight.
    std::vector<std::string> sauces;
    /// The order to offer ingredients in, when it matters (Manual Entry lists them
    /// in station order). Empty means alphabetical.
    std::vector<std::string> order;

    const Ingredient *find(const std::string &name) const;
    std::vector<std::string> names_of(Kind kind) const;

    bool is_sauce(const std::string &name) const
    {
        return contains(sauces, name);
    }

    /// A recipe's sauces, in the order it lists them: one cup each.
    std::vector<std::string> sauces_in(const Recipe &recipe) const;

    /// First matching filter wins, exactly as find_best_dynamic_portion_filter does.
    const FloorRule *floor_for(const std::vector<std::string> &bowl) const;
};

///
/// Reads the bridge-service menu dumps in `dir`. Each file carries a two-line
/// "ID:/MAPPINGS:" header ahead of the JSON body. Returns menus keyed by brand,
/// with warnings collected rather than thrown so one bad file cannot blank the app.
///
struct LoadResult
{
    std::vector<Menu> menus;
    std::vector<std::string> warnings; ///< a file could not be read or parsed
    /// The menu parsed, but some of its data is stale or inconsistent (a template
    /// naming an ingredient the brand does not list). Worth surfacing, not alarming.
    std::vector<std::string> issues;
};

///
/// A person's correction to how one brand names an ingredient: map it onto a common
/// name shared with other brands, and optionally fix its kind. Costs, curves, colours
/// and photos are all looked up by name, so a common name is what lets a renamed
/// ingredient pick them up. Stored in data/ingredient_aliases.csv.
///
struct NameMapping
{
    std::string name; ///< the common name; empty keeps the automatic one
    std::string kind; ///< "base", "protein", "topping" or "sauce"; empty keeps the guess

    bool operator==(const NameMapping &) const = default;
};

/// Keyed by brand, then the name exactly as that brand's menu spells it.
using NameMappings = std::map<std::pair<std::string, std::string>, NameMapping>;

/// The kinds a mapping may set, in the order an editor offers them.
extern const std::vector<std::string> kMappingKinds;

/// Reads brand,ingredient_menu,ingredient_name,kind rows. A missing file is not an
/// error (there is simply nothing mapped yet); a malformed one is.
bool load_name_mappings(const std::string &path, NameMappings &out, std::string *error = nullptr);

/// Writes the same columns, one row per entry, in brand then menu-name order.
bool save_name_mappings(const std::string &path, const NameMappings &mappings, std::string *error = nullptr);

///
/// Piece heights of chunky ingredients, keyed by the name in use (after mappings),
/// so every brand that serves an ingredient shares one height. Stored in
/// data/ingredient_pieces.csv as ingredient,piece_height_mm. An ingredient that is
/// not listed smears, except proteins, which take a placeholder until measured; a
/// listed 0 means "smears" explicitly.
///
using PieceHeights = std::map<std::string, double>;
constexpr double kPlaceholderProteinPieceMm = 20.0;

bool load_piece_heights(const std::string &path, PieceHeights &out, std::string *error = nullptr);
bool save_piece_heights(const std::string &path, const PieceHeights &pieces, std::string *error = nullptr);

///
/// Mound slopes (angle of repose, degrees) of ingredients that pile steeper or
/// flatter than the default, keyed by the name in use like piece heights. Stored in
/// data/ingredient_mounds.csv as ingredient,mound_slope_deg. Unlisted ingredients
/// take the default slope set in the window.
///
using MoundSlopes = std::map<std::string, double>;

bool load_mound_slopes(const std::string &path, MoundSlopes &out, std::string *error = nullptr);
bool save_mound_slopes(const std::string &path, const MoundSlopes &slopes, std::string *error = nullptr);

///
/// Roll-off angles (degrees) of chunky ingredients that roll off a slope steeper than
/// this even though a pile of them alone stands steeper: beef cubes rolling off a
/// couscous mound. Stored in data/ingredient_rolloff.csv as ingredient,roll_off_deg;
/// unlisted ingredients stay where they land, as before.
///
using RollOffAngles = std::map<std::string, double>;

bool load_roll_off(const std::string &path, RollOffAngles &out, std::string *error = nullptr);
bool save_roll_off(const std::string &path, const RollOffAngles &angles, std::string *error = nullptr);

///
/// Pooling groups for the fitted (Bayesian) curves: ingredients in a group share a
/// typical curve, so one with few or no measurements borrows from the others in
/// proportion to how little its own data say. Stored in data/ingredient_groups.csv as
/// ingredient,group, keyed by the name in use (names match regardless of case).
/// Unlisted ingredients take their automatic group: grain, green, protein, topping or sauce.
///
using IngredientGroups = std::map<std::string, std::string>;

/// The groups an ingredient falls into when nobody has set one, in display order.
extern const std::vector<std::string> kAutomaticGroups;

bool load_ingredient_groups(const std::string &path, IngredientGroups &out, std::string *error = nullptr);
bool save_ingredient_groups(const std::string &path, const IngredientGroups &groups, std::string *error = nullptr);

/// The brands in active use, one per line under a "brand" header (data/active_brands.csv).
/// Deprecated menus can stay in menus/ for reference; exports to lab37 leave them out. A
/// missing file is not an error and leaves `out` empty, which means every brand.
bool load_active_brands(const std::string &path, std::vector<std::string> &out, std::string *error = nullptr);

/// grain or green for a base (by family), protein, topping, or sauce.
std::string automatic_group(const std::string &name);
/// The listed group if there is one, otherwise the automatic group.
std::string pooling_group(const std::string &name, const IngredientGroups &groups);

/// The name a menu spelling is offered under, with prep and version words dropped:
/// "White Jasmine Rice retherm v11" -> "White Jasmine Rice". The same cleaning the
/// loader applies, for data typed straight from station or menu labels.
std::string clean_ingredient_name(const std::string &raw);

/// Base, protein or topping, read from the name alone, as the loader does when no
/// mapping says otherwise.
Kind guess_kind(const std::string &name);

/// On the culinary team's list of things served in sauce cups.
bool is_sauce_name(const std::string &name);

LoadResult load_menus(const std::string &dir,
                      const NameMappings *mappings = nullptr,
                      const PieceHeights *pieces = nullptr);

} // namespace bowlfill
