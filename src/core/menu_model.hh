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
    int weighted = 0;  ///< how many items carried a real weight in the menu

    int unweighted() const { return static_cast<int>(items.size()) - weighted; }
};

struct Menu {
    QString brand;
    QString version;
    std::map<QString, Ingredient> ingredients;
    std::vector<FloorRule> floors;
    std::vector<Recipe> recipes;
    bool has_split_rule = false;
    int split_rule_items = 0;

    const Ingredient *find(const QString &name) const;
    QStringList names_of(Kind kind) const;

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
    QStringList warnings;
};

LoadResult load_menus(const QString &dir);

}  // namespace bowlfill
