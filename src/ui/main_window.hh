#pragma once

#include "core/adaptive.hh"
#include "core/curves.hh"
#include "core/manual_entry.hh"
#include "core/measurements.hh"
#include "core/menu_model.hh"
#include "core/simulator.hh"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;

namespace bowlfill {

class RampChart;
class CurveChart;
class CurveFilter;
class BowlDiagram;
class ToleranceChart;
class PhotoLabel;
class ChipPicker;

/// Per-ingredient edits the user has made, keyed by ingredient name. Absent fields
/// fall back to the recipe's pinned weight, then the menu's configuration. Kept
/// across changes to the bowl and the recipe; cleared only when the brand changes.
struct Override {
    std::optional<double> start_g, step_g, max_g;
    std::optional<double> rate;          ///< oz/100 g, for an ingredient with no curve
    std::optional<double> curve_scale;   ///< multiplier on the curve, for one with a curve
};

/// The portion fields a chef edits in the finished-bowl table.
enum class Field { None, Start, Step, Max, Rate };

/// One cell of the finished-bowl table.
struct TableCell {
    QString text;
    Field field = Field::None;   ///< editable unless None
    double value = 0.0;          ///< in the unit the column shows
    bool edited = false;         ///< differs from the menu by the user's hand
};

/// One row of the finished-bowl table.
struct BreakdownRow {
    QString key;                ///< the ingredient's name, for edits
    QString name, kind;
    QColor swatch;              ///< the ingredient's band in the bowl diagram
    QColor colour;              ///< text colour; invalid means the default ink
    std::vector<TableCell> cells;
    bool edited = false;        ///< any field edited: the row offers a reset
};

/// A recipe as someone set it up: the bowl's picks and every edit, saved by name so
/// it can be brought back. Stored one per file in data/scenarios.
struct Scenario {
    QString name, brand, recipe;   ///< recipe empty for build-your-own
    bool adaptive = false;
    bool volume = false;           ///< Volume Fill (when not adaptive)
    QString base1, base2;
    QStringList proteins, toppings, sauces;
    bool split = true;
    std::optional<double> floor_g;
    std::map<QString, Override> overrides;
    QString file;                  ///< where it was read from
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &asset_dir, QWidget *parent = nullptr);

    /// Preselects a brand and, optionally, one of its recipes by name.
    void select(const QString &brand, const QString &recipe = {});
    void select_adaptive() { set_mode(true); }
    /// Writes the recipe spec of the bowl on screen to a PDF; false if it cannot.
    bool write_spec(const QString &path);

public slots:
    void set_mode(bool adaptive);
    void set_volume_fill() { set_model(Model::VolumeFill); }

private slots:
    void on_brand_changed();
    void on_recipe_changed();
    void on_selection_changed();
    void on_upload_csv();
    void on_reset_curves();
    void on_upload_bowl_data();
    void show_bowl_help();
    void show_csv_help();
    void show_report();
    void show_issues();
    void show_fit_state();
    void show_mappings();
    void toggle_theme();
    void set_units(bool metric);

private:
    void build_ui();
    QWidget *build_header();
    QWidget *build_controls();
    QWidget *build_results();
    void reload_theme();

    void populate_brand();
    void reload_menus();
    void update_mapping_note();
    void update_step_table();
    QString mappings_path() const;
    QString measurements_dir() const;
    /// Re-reads data/measurements into the curves and refits the squash; returns the
    /// files that could not be loaded.
    QStringList reload_measurements();
    QString store_and_reload(const QString &path, QStringList *problems);
    void update_curve_note();
    /// Loads data/fitted_curves.json into the curves and updates the header warning.
    void load_fitted();
    QString pieces_path() const;
    QString mounds_path() const;
    QString groups_path() const;
    QString rolloff_path() const;
    /// An ingredient's own mound slope, or -1 to take the default.
    double mound_slope_for(const QString &name) const;
    BowlGeometry geometry() const;
    DispenseVariation dispense() const;
    void update_fill_note(const SimResult &r);
    void update_geometry_note(double capacity_oz, const SauceCups &sauce,
                              const BowlGeometry &g, double chunk_mm);
    void rebuild_ingredient_pickers();
    QWidget *build_settings();
    void open_settings(bool open);
    /// Marks the drawer's pages whose settings differ from their defaults.
    void mark_settings();
    void reset_settings(int page);
    /// The volume per 100 g an ingredient is using now (its curve, scaled by any edit,
    /// at its start weight; or its flat rate), whether it has been edited, and why.
    struct RateInfo {
        double oz_per_100g = 0.0;
        bool edited = false;
        QString tip;
    };
    RateInfo rate_info(const QString &name) const;
    TableCell portion_cell(const QString &name, Field field, double grams) const;
    TableCell rate_cell(const QString &name) const;
    bool row_edited(const QString &name) const;
    QString ingredient_tip(const BowlItem &it) const;
    void on_portion_edited(int row, int column, double value);
    /// Refills the finished-bowl table, keeping an open editor where it was.
    void fill_portion_table(const std::vector<BreakdownRow> &rows, const QStringList &headers,
                            const std::map<int, QString> &tips);
    /// The finished-bowl note's mounding line, empty with mounding off.
    QString mound_line(const SimResult &r) const;
    /// Takes an edited rate: scales the curve to match it, or sets the flat rate.
    void set_rate(const QString &name, double oz_per_100g);
    void apply_floor();
    void recompute();
    void render_adaptive();
    void render_photos(const std::vector<BowlItem> &items);

    const Menu *menu() const;
    SimSettings legacy_settings() const;
    AdaptiveSettings adaptive_settings() const;
    double value_for(const QString &name, const QString &field) const;
    std::vector<BowlItem> assemble_bowl() const;
    QStringList bowl_names() const;

    QString asset_dir_;
    std::vector<Menu> menus_;   ///< the loaded brands, then Manual Entry last

    // Manual Entry: the robot's stations, edited on their own settings page.
    ManualEntry manual_;
    struct StationRow {
        QComboBox *name = nullptr;
        QComboBox *kind = nullptr;
    };
    std::vector<StationRow> station_rows_;
    ChipPicker *manual_sauces_ = nullptr;
    /// Rebuilds the Manual Entry brand from manual_ in menus_ (adding it if missing).
    void rebuild_manual_menu();
    /// Saves manual_ and rebuilds the brand, refreshing the bowl if it is showing.
    void apply_manual_entry();
    /// Refills the station dropdowns from the loaded brands, keeping what each holds.
    void refresh_station_catalog();
    /// The loaded brands without Manual Entry.
    std::vector<Menu> loaded_menus() const;
    CurveSet curves_;
    MeasurementSet measurements_;   ///< what was loaded from data/measurements
    QDoubleSpinBox *empty_bowl_ml_ = nullptr;   ///< the camera's zero, applied to scans
    QCheckBox *backfill_ = nullptr;             ///< correct scans by the test-beans reference
    QCheckBox *drop_impossible_ = nullptr;      ///< leave impossible readings out of fits
    SquashFit squash_;              ///< fitted over every mixed bowl on file
    QStringList load_warnings_;

    std::map<QString, Override> overrides_;

    // The settings drawer.
    QStackedWidget *body_ = nullptr;   ///< the bowl view, or the settings
    QLabel *settings_summary_ = nullptr;   ///< the current bowl, in one line
    void update_settings_summary(const FillUncertainty &u);
    // Saved scenarios.
    QString scenarios_dir() const;
    std::vector<Scenario> scenarios_;     ///< this brand's, by name
    QComboBox *scenario_ = nullptr;
    QPushButton *scenario_delete_ = nullptr;
    void reload_scenarios(const QString &select = {});
    void save_scenario();
    void apply_scenario(const Scenario &s);
    void delete_scenario();
    /// A one-page PDF of the bowl on screen, for handing to culinary.
    void export_spec();
    bool row_edited_any() const;
    QImage spec_image_;   ///< the diagram, held while the spec document is drawn
    /// Opens the settings on one of its pages.
    void show_settings_page(int page);
    /// What the current bowl's numbers rest on that is not measured: the header
    /// button lists them, each leading to where it is set.
    struct Estimate {
        QString text;
        std::function<void()> go;   ///< empty when there is nowhere in the app to fix it
    };
    std::vector<Estimate> estimates_list_;
    QPushButton *estimates_ = nullptr;
    void update_estimates(const std::vector<BowlItem> &items, const AdaptiveResult *ar);
    /// Fills the Bowl COGS tile from the finished bowl's final masses.
    void show_bowl_cost(const std::vector<BowlItem> &items);
    QPushButton *settings_ = nullptr;
    std::vector<QPushButton *> settings_nav_;
    std::vector<QPushButton *> settings_resets_;   ///< one per page
    struct TrackedSetting {
        int page;
        std::function<bool()> changed;
        std::function<void()> restore;
    };
    std::vector<TrackedSetting> settings_tracked_;
    QPushButton *reset_edits_ = nullptr;
    /// Start weights the selected recipe pins, by ingredient. Replaced on every
    /// recipe change, unlike overrides_, which only a brand change clears.
    std::map<QString, double> pins_;
    std::optional<double> floor_override_;   ///< a weight floor the user typed
    QStringList picked_proteins_, picked_toppings_;
    QStringList picked_sauces_;   ///< one sauce cup each, up to SauceCups::kMax
    int sauce_cup_count() const;
    SimResult last_;
    AdaptiveResult last_adaptive_;
    CostTable costs_;
    /// Which dispense model runs: the menu's dynamic ramp, Adaptive fill, or the
    /// ramp made to respect the bowl (Volume Fill).
    enum class Model { Dynamic, Adaptive, VolumeFill };
    Model model_ = Model::Dynamic;
    bool adaptive_ = false;   ///< model_ == Model::Adaptive
    void set_model(Model model);
    VolumeFillSettings volume_fill_settings() const;
    QPushButton *mode_volume_ = nullptr;
    QLabel *floor_panel_title_ = nullptr;
    QWidget *volume_fields_ = nullptr;
    QWidget *floor_fields_ = nullptr;   ///< the weight floor, hidden under Volume Fill
    QDoubleSpinBox *fit_pct_ = nullptr;
    QDoubleSpinBox *min_pct_ = nullptr;

    // Controls
    QComboBox *brand_ = nullptr;
    QComboBox *recipe_ = nullptr;
    QComboBox *base1_ = nullptr;
    QComboBox *base2_ = nullptr;
    QDoubleSpinBox *floor_ = nullptr;
    QDoubleSpinBox *capacity_ = nullptr;
    QDoubleSpinBox *load_transfer_ = nullptr;
    QCheckBox *split_ = nullptr;
    QCheckBox *compress_ = nullptr;
    QPushButton *theme_ = nullptr;
    QPushButton *mode_legacy_ = nullptr;
    QPushButton *mode_adaptive_ = nullptr;
    QWidget *legacy_group_ = nullptr;
    QWidget *adaptive_group_ = nullptr;
    QDoubleSpinBox *target_fill_ = nullptr;
    QDoubleSpinBox *band_low_ = nullptr;
    QDoubleSpinBox *tol_base_ = nullptr;
    QDoubleSpinBox *tol_protein_ = nullptr;
    QDoubleSpinBox *tol_topping_ = nullptr;
    QDoubleSpinBox *menu_price_ = nullptr;
    QDoubleSpinBox *cogs_target_ = nullptr;
    QWidget *ramp_panel_ = nullptr;
    QWidget *tol_panel_ = nullptr;
    ToleranceChart *tol_chart_ = nullptr;
    QPushButton *unit_oz_ = nullptr;
    QPushButton *unit_ml_ = nullptr;
    QLabel *capacity_label_ = nullptr;
    QPushButton *about_ = nullptr;
    QPushButton *issues_ = nullptr;   ///< shown only when the menus carry data issues
    QPushButton *fit_state_ = nullptr; ///< shown when fitted curves are missing or out of date
    QString fit_message_;              ///< why, for the header button and the curve note
    QStringList menu_issues_;
    NameMappings name_maps_;           ///< data/ingredient_aliases.csv
    PieceHeights pieces_;              ///< data/ingredient_pieces.csv
    IngredientGroups groups_;          ///< data/ingredient_groups.csv
    MoundSlopes mound_slopes_;         ///< data/ingredient_mounds.csv
    RollOffAngles roll_off_;           ///< data/ingredient_rolloff.csv
    QCheckBox *heights_ = nullptr;     ///< judge fit by height under the lid
    QPushButton *cups_pressed_ = nullptr;
    QPushButton *cups_resting_ = nullptr;
    QWidget *cups_segment_ = nullptr;
    QDoubleSpinBox *depth_ = nullptr;
    QDoubleSpinBox *headroom_ = nullptr;
    QDoubleSpinBox *bowl_top_d_ = nullptr;
    QDoubleSpinBox *bowl_bottom_d_ = nullptr;
    QDoubleSpinBox *lid_plateau_d_ = nullptr;
    QDoubleSpinBox *cup_h_ = nullptr;
    QDoubleSpinBox *cup_d_ = nullptr;
    QDoubleSpinBox *cup_base_d_ = nullptr;
    QDoubleSpinBox *chunk_proud_ = nullptr;
    QDoubleSpinBox *mound_slope_ = nullptr;
    QCheckBox *mound_ = nullptr;
    QLabel *fill_note_ = nullptr;             ///< median, 80% range and overfill odds
    QDoubleSpinBox *disp_base_ = nullptr;      ///< dispense variation, % of target, 1 sd
    QDoubleSpinBox *disp_protein_ = nullptr;
    QDoubleSpinBox *disp_topping_ = nullptr;
    QDoubleSpinBox *bias_base_ = nullptr;
    QDoubleSpinBox *bias_protein_ = nullptr;
    QDoubleSpinBox *bias_topping_ = nullptr;
    QLabel *geometry_note_ = nullptr;
    QLabel *mapping_note_ = nullptr;

    ChipPicker *protein_chips_ = nullptr;
    ChipPicker *topping_chips_ = nullptr;
    ChipPicker *sauce_chips_ = nullptr;

    // Readouts
    QLabel *brand_note_ = nullptr;
    // Volume per gram: what each ingredient's step adds, for choosing step_g.
    QDoubleSpinBox *step_target_ml_ = nullptr;
    QTableWidget *step_table_ = nullptr;
    QLabel *step_note_ = nullptr;
    QLabel *recipe_note_ = nullptr;
    QLabel *floor_note_ = nullptr;
    QPushButton *floor_reset_ = nullptr;
    QLabel *compress_note_ = nullptr;
    QLabel *curve_note_ = nullptr;
    QLabel *verdict_head_ = nullptr;
    QLabel *verdict_sub_ = nullptr;
    QWidget *verdict_panel_ = nullptr;
    /// The last tile is the bowl's food cost, the same in every mode.
    static constexpr int kStatTiles = 6;
    QLabel *stat_value_[kStatTiles] = {};
    QLabel *stat_key_[kStatTiles] = {};
    QWidget *stat_tile_[kStatTiles] = {};
    QTableWidget *breakdown_ = nullptr;
    QLabel *foot_note_ = nullptr;
    PhotoLabel *photo_[2] = {nullptr, nullptr};
    QLabel *photo_caption_[2] = {nullptr, nullptr};

    RampChart *ramp_chart_ = nullptr;
    CurveChart *curve_chart_ = nullptr;
    CurveFilter *curve_filter_ = nullptr;   ///< which curves the chart shows
    BowlDiagram *diagram_ = nullptr;
    QWidget *bowl_row_ = nullptr;   ///< the finished-bowl card; rearranges with width

    bool loading_ = false;   ///< suppresses recompute while widgets are repopulated
};

}  // namespace bowlfill
