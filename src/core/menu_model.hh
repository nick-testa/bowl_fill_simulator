#pragma once

#include <QString>
#include <QStringList>
#include <map>
#include <optional>
#include <vector>

namespace bowlfill {

enum class Kind { Base, Protein, Topping };

QString to_string(Kind kind);

///
/// How an ingredient's portion weight was established. Anything below Slug is a
/// guess and is surfaced to the user rather than passed off as configuration.
///
enum class WeightSource {
    Menu,      ///< named outright in mappings.ingredients
    Slug,      ///< matched by name tokens against a standalone menu_item_id
    Alias,     ///< matched via a "no X" removal template's dish slug
    Template,  ///< only ever served inside a preconfigured bowl
    Class,     ///< borrowed from the modal weight of its class and family
    Recipe,    ///< appears only in a recipe, with no ramp settings of its own
    None,
};

QString to_string(WeightSource source);

struct Ingredient {
    QString name;
    Kind kind = Kind::Topping;
    double step_increment_g = 0.0;
    double max_dispense_weight_g = 0.0;
    double per_portion_minimum_weight_g = 0.0;
    std::vector<double> weights;      ///< standalone (build-your-own) portions, ascending
    std::vector<double> weights_tpl;  ///< portions seen only inside preconfigured bowls
    WeightSource source = WeightSource::None;

    /// Height of one piece, for chunky ingredients; 0 means it smears. Comes from
    /// data/ingredient_pieces.csv, or a placeholder for proteins until measured.
    double piece_height_mm = 0.0;
    bool piece_height_placeholder = false;

    /// The largest configured standalone portion: what a build-your-own bowl starts
    /// its ramp from, and the worst case for volume.
    double full_portion() const { return weights.empty() ? 0.0 : weights.back(); }
};

enum class FilterOp { ExactlyOne, Any };

///
/// One entry of dynamic_portion_increases[].apply_for. Mirrors
/// DynamicPortionAlgorithmFilter in lab37/orders/menu_mappings.rbuf.
///
struct FloorRule {
    QStringList must_have;
    QStringList must_not_have;
    FilterOp op = FilterOp::Any;
    double minimum_product_weight_g = 0.0;

    bool matches(const QStringList &bowl) const;
};

struct RecipeItem {
    QString name;
    double grams = 0.0;
};

struct Recipe {
    QString name;
    std::vector<RecipeItem> items;
    int weighted = 0;      ///< how many items carried a real weight in the menu
    /// A template with no target_item_ids: the customer picks every ingredient.
    /// Not offered in the recipe picker, but still audited.
    bool customer_built = false;

    int unweighted() const { return static_cast<int>(items.size()) - weighted; }
};

///
/// One spelling of an ingredient as a menu uses it, and what the loader made of it.
/// The mapping editor lists these; they are what a NameMapping is keyed on.
///
struct MenuName {
    QString raw;           ///< as the menu JSON spells it
    QString automatic;     ///< the name the loader derives with no mapping applied
    QString name;          ///< the name in use, after any mapping
    QString kind;          ///< "base", "protein", "topping" or "sauce", after any mapping
    QString guessed_kind;  ///< the kind the loader guesses from the automatic name
};

struct Menu {
    QString brand;
    QString version;
    std::map<QString, Ingredient> ingredients;
    std::vector<FloorRule> floors;
    std::vector<Recipe> recipes;
    bool has_split_rule = false;
    int split_rule_items = 0;
    std::vector<MenuName> names;   ///< every spelling the menu used, sorted by raw
    /// What the brand serves in sauce cups, sorted. Never in `ingredients`: a sauce
    /// takes a cup's room in the bowl, not a dispensed weight.
    QStringList sauces;

    const Ingredient *find(const QString &name) const;
    QStringList names_of(Kind kind) const;
    bool is_sauce(const QString &name) const { return sauces.contains(name); }
    /// A recipe's sauces, in the order it lists them: one cup each.
    QStringList sauces_in(const Recipe &recipe) const;

    /// First matching filter wins, exactly as find_best_dynamic_portion_filter does.
    const FloorRule *floor_for(const QStringList &bowl) const;
};

///
/// Reads the bridge-service menu dumps in `dir`. Each file carries a two-line
/// "ID:/MAPPINGS:" header ahead of the JSON body. Returns menus keyed by brand,
/// with warnings collected rather than thrown so one bad file cannot blank the app.
///
struct LoadResult {
    std::vector<Menu> menus;
    QStringList warnings;   ///< a file could not be read or parsed
    /// The menu parsed, but some of its data is stale or inconsistent (a template
    /// naming an ingredient the brand does not list). Worth surfacing, not alarming.
    QStringList issues;
};

///
/// A person's correction to how one brand names an ingredient: map it onto a common
/// name shared with other brands, and optionally fix its kind. Costs, curves, colours
/// and photos are all looked up by name, so a common name is what lets a renamed
/// ingredient pick them up. Stored in data/ingredient_aliases.csv.
///
struct NameMapping {
    QString name;   ///< the common name; empty keeps the automatic one
    QString kind;   ///< "base", "protein", "topping" or "sauce"; empty keeps the guess

    bool operator==(const NameMapping &) const = default;
};

/// Keyed by brand, then the name exactly as that brand's menu spells it.
using NameMappings = std::map<std::pair<QString, QString>, NameMapping>;

/// The kinds a mapping may set, in the order an editor offers them.
extern const QStringList kMappingKinds;

/// Reads brand,ingredient_menu,ingredient_name,kind rows. A missing file is not an
/// error (there is simply nothing mapped yet); a malformed one is.
bool load_name_mappings(const QString &path, NameMappings &out, QString *error = nullptr);

/// Writes the same columns, one row per entry, in brand then menu-name order.
bool save_name_mappings(const QString &path, const NameMappings &mappings,
                        QString *error = nullptr);

///
/// Piece heights of chunky ingredients, keyed by the name in use (after mappings),
/// so every brand that serves an ingredient shares one height. Stored in
/// data/ingredient_pieces.csv as ingredient,piece_height_mm. An ingredient that is
/// not listed smears, except proteins, which take a placeholder until measured; a
/// listed 0 means "smears" explicitly.
///
using PieceHeights = std::map<QString, double>;
constexpr double kPlaceholderProteinPieceMm = 20.0;

bool load_piece_heights(const QString &path, PieceHeights &out, QString *error = nullptr);
bool save_piece_heights(const QString &path, const PieceHeights &pieces,
                        QString *error = nullptr);

LoadResult load_menus(const QString &dir, const NameMappings *mappings = nullptr,
                      const PieceHeights *pieces = nullptr);

}  // namespace bowlfill
