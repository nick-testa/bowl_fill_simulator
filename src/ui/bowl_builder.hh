#pragma once

#include "core/costs.hh"
#include "core/menu_model.hh"
#include "core/simulator.hh"

#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>
#include <optional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QVBoxLayout;
class QLabel;
class QPushButton;

namespace bowlfill {

class BowlEditor;
class ChipPicker;

/// What the builder needs from the window: the menus, how an ingredient becomes a bowl
/// item, and the bowl, lid, compression and dispense settings everything else uses.
struct BuilderContext {
    const std::vector<Menu> *menus = nullptr;
    const CostTable *costs = nullptr;
    std::function<BowlItem(const Ingredient &)> make_item;
    /// The window's settings; the builder sets the sauce cups and never ramps.
    std::function<SimSettings()> settings;
    /// The most the food may fill, as a share of the food room, and where that comes from.
    std::function<double(const Menu &)> max_fill;
    std::function<QString(const Menu &)> max_fill_source;
    /// The overfill chance a bowl is flagged at (Settings → Dispense).
    std::function<double()> risk_limit;
};

/// A bowl as the builder has it, for Dynamic Portioning to start from.
struct ComposedBowl {
    QString brand;
    QStringList bases, proteins, toppings, sauces;
    std::map<QString, double> grams;   ///< every dispensed ingredient's portion
    int revision = 0;                  ///< bumped on every change
};

///
/// The main view: a chef picks a menu's ingredients and sets how much of the bowl each
/// should fill, on a slider or by typing a percentage. Shares are of the food room (the
/// bowl less what the sauce cups and chunks need), so they add up to how full the bowl
/// is; each is turned into grams through its ingredient's curve, compression included,
/// and the bowl is shown with its weight, volume and food cost.
///
/// A new bowl starts at the menu's portions, scaled together to the max fill. An
/// ingredient added later comes in at its menu portion; Fill to max scales them all.
///
class BowlBuilder : public QWidget {
    Q_OBJECT

public:
    explicit BowlBuilder(BuilderContext context, QWidget *parent = nullptr);

    /// The menus were reloaded: refill the brand list, keeping the brand and bowl.
    void reload_menus();
    /// Selects a menu and, optionally, the recipe the bowl starts from.
    void select_brand(const QString &brand, const QString &recipe = {});
    /// Starts the bowl again from the menu's portions, as on a brand change.
    void restart() { on_start_changed(); }
    /// The settings changed: recompute the bowl as it stands.
    void refresh();
    ComposedBowl composed() const { return composed_; }

signals:
    void changed();

private:
    const Menu *menu() const;
    void on_brand_changed();
    void on_start_changed();
    void on_picks_changed();
    /// The shares each ingredient starts at: a pinned one at its recipe weight, the rest
    /// at their menu portions, scaled together so the bowl sits at `total` when one is given.
    void portion_shares(const std::map<QString, double> &pinned, std::optional<double> total);
    /// The portion an ingredient comes in at, before any share is set.
    double menu_portion(const QString &name, const std::map<QString, double> &pinned) const;
    void set_share(const QString &name, double share);
    void scale_to(double total);
    QStringList dispensed() const;   ///< bases, proteins, toppings, in dispense order
    std::vector<BowlItem> items() const;
    SimSettings bowl_settings(const std::vector<BowlItem> &items) const;
    double room_oz() const;
    void rebuild_rows();
    /// A layer's top was dragged to `top_oz` of food: that ingredient takes what is
    /// between it and the layers below.
    void on_edge_moved(int index, double top_oz);
    void recompute();

    BuilderContext ctx_;
    bool loading_ = false;

    QComboBox *brand_ = nullptr;
    QComboBox *start_ = nullptr;
    QLabel *start_note_ = nullptr;
    ChipPicker *bases_ = nullptr, *proteins_ = nullptr, *toppings_ = nullptr, *sauces_ = nullptr;

    BowlEditor *editor_ = nullptr;
    QLabel *fill_value_ = nullptr, *fill_note_ = nullptr;
    QLabel *weight_value_ = nullptr, *volume_value_ = nullptr, *cost_value_ = nullptr;
    QLabel *risk_note_ = nullptr;
    QPushButton *to_max_ = nullptr;   ///< scales the proportions to a full bowl
    QVBoxLayout *rows_ = nullptr;
    QLabel *empty_note_ = nullptr;
    QLabel *sauce_note_ = nullptr;

    /// One row of the side panel per dispensed ingredient, in dispense order.
    struct Row {
        QString name;
        QWidget *box = nullptr;
        QLabel *swatch = nullptr, *label = nullptr, *amount = nullptr, *cost = nullptr;
        QDoubleSpinBox *pct = nullptr;
    };
    std::vector<Row> row_widgets_;

    std::vector<double> last_oz_;   ///< each layer's volume, as last drawn
    double last_room_ = 0.0;        ///< the food room, as last drawn
    std::map<QString, double> share_;   ///< of the food room, by ingredient
    std::map<QString, double> pinned_;  ///< grams the starting recipe sets, by ingredient
    bool as_written_ = false;           ///< started from a named recipe, not filled to the max
    ComposedBowl composed_;
};

}  // namespace bowlfill
