#pragma once

#include "core/curves.hh"
#include "core/menu_model.hh"
#include "core/simulator.hh"

#include <QHash>
#include <QMainWindow>
#include <map>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace bowlfill {

class RampChart;
class CurveChart;
class BowlDiagram;

/// Per-ingredient edits the user has made, keyed by ingredient name. Absent fields
/// fall back to the menu's configuration.
struct Override {
    std::optional<double> start_g, step_g, max_g, rate;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &asset_dir, QWidget *parent = nullptr);

    /// Preselects a brand and, optionally, one of its recipes by name.
    void select(const QString &brand, const QString &recipe = {});

private slots:
    void on_brand_changed();
    void on_recipe_changed();
    void on_selection_changed();
    void on_upload_csv();
    void on_reset_curves();
    void show_csv_help();
    void toggle_theme();
    void set_units(bool metric);

private:
    void build_ui();
    QWidget *build_controls();
    QWidget *build_results();
    void reload_theme();

    void populate_brand();
    void rebuild_ingredient_pickers();
    void rebuild_override_rows();
    void apply_floor();
    void recompute();

    const Menu *menu() const;
    Method method() const;
    double value_for(const QString &name, const QString &field) const;
    std::vector<BowlItem> assemble_bowl() const;
    QStringList bowl_names() const;

    QString asset_dir_;
    std::vector<Menu> menus_;
    CurveSet curves_;
    QStringList load_warnings_;

    std::map<QString, Override> overrides_;
    QStringList picked_proteins_, picked_toppings_;
    SimResult last_;

    // Controls
    QComboBox *brand_ = nullptr;
    QComboBox *recipe_ = nullptr;
    QComboBox *base1_ = nullptr;
    QComboBox *base2_ = nullptr;
    QDoubleSpinBox *floor_ = nullptr;
    QDoubleSpinBox *capacity_ = nullptr;
    QDoubleSpinBox *load_transfer_ = nullptr;
    QPushButton *method_robot_ = nullptr;
    QPushButton *method_hand_ = nullptr;
    QPushButton *method_pooled_ = nullptr;
    QPushButton *split_ = nullptr;
    QPushButton *compress_ = nullptr;
    QPushButton *theme_ = nullptr;
    QPushButton *unit_oz_ = nullptr;
    QPushButton *unit_ml_ = nullptr;
    QLabel *capacity_label_ = nullptr;
    QLabel *rate_header_ = nullptr;
    QLabel *intro_ = nullptr;

    QWidget *protein_picks_ = nullptr;
    QWidget *topping_picks_ = nullptr;
    QTableWidget *override_table_ = nullptr;
    QGridLayout *base_card_layout_[2] = {nullptr, nullptr};
    QDoubleSpinBox *base_start_[2] = {nullptr, nullptr};
    QDoubleSpinBox *base_step_[2] = {nullptr, nullptr};
    QDoubleSpinBox *base_max_[2] = {nullptr, nullptr};
    QLabel *base_fit_[2] = {nullptr, nullptr};
    QWidget *base_card_[2] = {nullptr, nullptr};

    // Readouts
    QLabel *brand_note_ = nullptr;
    QLabel *recipe_note_ = nullptr;
    QLabel *floor_note_ = nullptr;
    QLabel *method_note_ = nullptr;
    QLabel *split_note_ = nullptr;
    QLabel *compress_note_ = nullptr;
    QLabel *curve_note_ = nullptr;
    QLabel *verdict_head_ = nullptr;
    QLabel *verdict_sub_ = nullptr;
    QWidget *verdict_panel_ = nullptr;
    QLabel *stat_value_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QWidget *stat_tile_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QTableWidget *breakdown_ = nullptr;
    QLabel *foot_note_ = nullptr;
    QLabel *photo_[2] = {nullptr, nullptr};
    QLabel *photo_caption_[2] = {nullptr, nullptr};

    RampChart *ramp_chart_ = nullptr;
    CurveChart *curve_chart_ = nullptr;
    BowlDiagram *diagram_ = nullptr;

    bool loading_ = false;   ///< suppresses recompute while widgets are repopulated
};

}  // namespace bowlfill
