#include "main_window.hh"

#include "bowl_diagram.hh"
#include "curve_chart.hh"
#include "ramp_chart.hh"
#include "theme.hh"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

constexpr double kFlatProteinRate = 3.5;   // oz/100 g; nothing here was measured
constexpr double kFlatToppingRate = 4.5;

QLabel *make_label(const QString &text, const char *role)
{
    auto *l = new QLabel(text);
    l->setObjectName(role);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

QFrame *make_panel(const char *role = "panel")
{
    auto *f = new QFrame;
    f->setObjectName(role);
    return f;
}

QFrame *hline()
{
    auto *f = new QFrame;
    f->setObjectName("sep");
    f->setFixedHeight(1);
    return f;
}

QDoubleSpinBox *make_spin(double lo, double hi, double step, int decimals)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

QWidget *field(const QString &caption, QWidget *w)
{
    auto *box = new QWidget;
    box->setObjectName("clear");
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    auto *l = new QLabel(caption);
    l->setObjectName("fieldLabel");
    v->addWidget(l);
    v->addWidget(w);
    return box;
}

QPushButton *make_toggle(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setCheckable(true);
    return b;
}

QString fmt(double v, int dp = 1) { return QString::number(v, 'f', dp); }

}  // namespace

//
// ############################################################################
//

MainWindow::MainWindow(const QString &asset_dir, QWidget *parent)
    : QMainWindow(parent), asset_dir_(asset_dir)
{
    LoadResult loaded = load_menus(asset_dir_ + "/menus");
    menus_ = std::move(loaded.menus);
    load_warnings_ = loaded.warnings;

    QString err;
    if (!curves_.load_csv(asset_dir_ + "/data/mass_to_volume.csv", &err))
        load_warnings_ << QString("mass_to_volume.csv: %1").arg(err);

    build_ui();
    reload_theme();
    populate_brand();

    if (!load_warnings_.isEmpty())
        QMessageBox::warning(this, "Loaded with warnings", load_warnings_.join("\n"));
}

void MainWindow::select(const QString &brand, const QString &recipe)
{
    if (const int i = brand_->findData(brand); i >= 0) brand_->setCurrentIndex(i);
    if (recipe.isEmpty()) return;
    if (const int i = recipe_->findData(recipe); i >= 0) recipe_->setCurrentIndex(i);
}

void MainWindow::build_ui()
{
    setWindowTitle("Bowl Fill Simulator");
    resize(1400, 920);

    auto *central = new QWidget;
    central->setObjectName("clear");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(make_label("LAB37 · ASSEMBLY LINE PORTIONING", "eyebrow"));
    auto *h1 = new QLabel("Bowl Fill Simulator");
    h1->setObjectName("h1");
    titles->addWidget(h1);
    titles->addWidget(make_label(
        "The dynamic portion algorithm optimises on <i>mass</i> — it steps every "
        "ingredient until the bowl clears its recipe's gram floor. Bowls fail at the "
        "lidder on <i>volume</i>. Ramp settings, portion weights and floors are read "
        "from the menu dumps in <code>menus/</code>; mass→volume is "
        "<code>oz = K·g^p</code>, fitted to the measured bowls.",
        "note"));
    header->addLayout(titles, 1);

    theme_ = new QPushButton("◐  Theme");
    connect(theme_, &QPushButton::clicked, this, &MainWindow::toggle_theme);
    header->addWidget(theme_, 0, Qt::AlignTop);
    root->addLayout(header);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(build_controls());
    splitter->addWidget(build_results());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({390, 1010});
    root->addWidget(splitter, 1);

    setCentralWidget(central);
}

QWidget *MainWindow::build_controls()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(360);

    auto *panel = make_panel();
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(15, 15, 15, 15);
    v->setSpacing(13);

    // ---- menu -------------------------------------------------------------
    v->addWidget(make_label("MENU", "eyebrow"));
    brand_ = new QComboBox;
    connect(brand_, &QComboBox::currentIndexChanged, this, &MainWindow::on_brand_changed);
    v->addWidget(field("brand", brand_));

    recipe_ = new QComboBox;
    connect(recipe_, &QComboBox::currentIndexChanged, this, &MainWindow::on_recipe_changed);
    v->addWidget(field("bowl recipe", recipe_));
    recipe_note_ = make_label("", "note");
    v->addWidget(recipe_note_);
    brand_note_ = make_label("", "note");
    v->addWidget(brand_note_);
    v->addWidget(hline());

    // ---- weight floor -----------------------------------------------------
    v->addWidget(make_label("WEIGHT FLOOR", "eyebrow"));
    floor_ = make_spin(0, 5000, 5, 0);
    connect(floor_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    v->addWidget(field("minimum_product_weight_g — matched from the menu", floor_));
    floor_note_ = make_label("", "note");
    v->addWidget(floor_note_);
    v->addWidget(hline());

    // ---- measurement source ----------------------------------------------
    v->addWidget(make_label("MEASUREMENT SOURCE", "eyebrow"));
    auto *seg = new QHBoxLayout;
    seg->setSpacing(4);
    method_robot_ = make_toggle("robot");
    method_hand_ = make_toggle("hand");
    method_pooled_ = make_toggle("pooled");
    method_robot_->setChecked(true);
    for (QPushButton *b : {method_robot_, method_hand_, method_pooled_}) {
        seg->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, b] {
            for (QPushButton *o : {method_robot_, method_hand_, method_pooled_})
                o->setChecked(o == b);
            on_selection_changed();
        });
    }
    v->addLayout(seg);
    method_note_ = make_label("", "note");
    v->addWidget(method_note_);
    v->addWidget(hline());

    // ---- bases ------------------------------------------------------------
    v->addWidget(make_label("BASE INGREDIENTS (UP TO 2)", "eyebrow"));
    for (int i = 0; i < 2; ++i) {
        QComboBox *&box = (i == 0 ? base1_ : base2_);
        box = new QComboBox;
        connect(box, &QComboBox::currentIndexChanged, this,
                &MainWindow::on_selection_changed);
        v->addWidget(field(i == 0 ? "Base 1 — bottom layer" : "Base 2", box));

        auto *card = make_panel("card");
        auto *g = new QGridLayout(card);
        g->setContentsMargins(11, 11, 11, 11);
        g->setSpacing(7);
        base_start_[i] = make_spin(0, 2000, 1, 0);
        base_step_[i] = make_spin(0, 100, 0.1, 2);
        base_max_[i] = make_spin(0, 2000, 1, 0);
        g->addWidget(field("start g", base_start_[i]), 0, 0);
        g->addWidget(field("step g", base_step_[i]), 0, 1);
        g->addWidget(field("max g", base_max_[i]), 0, 2);
        base_fit_[i] = make_label("", "note");
        base_fit_[i]->setFont(theme::mono(8));
        g->addWidget(base_fit_[i], 1, 0, 1, 3);
        for (QDoubleSpinBox *s : {base_start_[i], base_step_[i], base_max_[i]})
            connect(s, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
        base_card_[i] = card;
        base_card_layout_[i] = g;
        v->addWidget(card);
    }
    split_ = make_toggle("Apply the 50:50 base split");
    connect(split_, &QPushButton::clicked, this, &MainWindow::on_selection_changed);
    v->addWidget(split_);
    split_note_ = make_label("", "note");
    v->addWidget(split_note_);
    v->addWidget(hline());

    // ---- proteins & toppings ---------------------------------------------
    v->addWidget(make_label("PROTEINS (UP TO 2)", "eyebrow"));
    protein_picks_ = new QWidget;
    protein_picks_->setObjectName("clear");
    new QGridLayout(protein_picks_);
    protein_picks_->layout()->setContentsMargins(0, 0, 0, 0);
    v->addWidget(protein_picks_);

    v->addWidget(make_label("OTHER TOPPINGS", "eyebrow"));
    topping_picks_ = new QWidget;
    topping_picks_->setObjectName("clear");
    new QGridLayout(topping_picks_);
    topping_picks_->layout()->setContentsMargins(0, 0, 0, 0);
    v->addWidget(topping_picks_);

    override_table_ = new QTableWidget(0, 5);
    override_table_->setHorizontalHeaderLabels(
        {"ingredient", "start g", "step g", "max g", "oz/100g"});
    override_table_->verticalHeader()->setVisible(false);
    override_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c)
        override_table_->horizontalHeader()->setSectionResizeMode(c,
                                                                  QHeaderView::Fixed);
    override_table_->setColumnWidth(1, 62);
    override_table_->setColumnWidth(2, 58);
    override_table_->setColumnWidth(3, 58);
    override_table_->setColumnWidth(4, 62);
    override_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    override_table_->setSelectionMode(QAbstractItemView::NoSelection);
    v->addWidget(override_table_);
    v->addWidget(hline());

    // ---- bowl -------------------------------------------------------------
    v->addWidget(make_label("BOWL", "eyebrow"));
    capacity_ = make_spin(1, 200, 1, 0);
    capacity_->setValue(32);
    connect(capacity_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    v->addWidget(field("bowl capacity oz", capacity_));

    compress_ = make_toggle("Compress the base under this load");
    connect(compress_, &QPushButton::clicked, this, &MainWindow::on_selection_changed);
    v->addWidget(compress_);
    load_transfer_ = make_spin(0, 1, 0.05, 2);
    load_transfer_->setValue(1.0);
    connect(load_transfer_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    v->addWidget(field("load transfer — share of topping weight bearing on the base",
                       load_transfer_));
    compress_note_ = make_label("", "note");
    v->addWidget(compress_note_);
    v->addWidget(hline());

    // ---- measured data ----------------------------------------------------
    v->addWidget(make_label("MASS → VOLUME DATA", "eyebrow"));
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    auto *upload = new QPushButton("Upload CSV…");
    upload->setObjectName("primary");
    connect(upload, &QPushButton::clicked, this, &MainWindow::on_upload_csv);
    row->addWidget(upload, 1);

    auto *help = new QPushButton("?");
    help->setObjectName("info");
    help->setCursor(Qt::WhatsThisCursor);
    help->setToolTip(CurveSet::format_help());
    connect(help, &QPushButton::clicked, this, &MainWindow::show_csv_help);
    row->addWidget(help, 0);

    auto *reset = new QPushButton("Reset");
    reset->setToolTip("Discard uploaded rows and return to the bundled measurements.");
    connect(reset, &QPushButton::clicked, this, &MainWindow::on_reset_curves);
    row->addWidget(reset, 0);
    v->addLayout(row);

    curve_note_ = make_label("", "note");
    v->addWidget(curve_note_);

    v->addStretch(1);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::build_results()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *host = new QWidget;
    host->setObjectName("clear");
    auto *v = new QVBoxLayout(host);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(14);

    verdict_panel_ = make_panel();
    auto *vv = new QVBoxLayout(verdict_panel_);
    vv->setContentsMargins(15, 13, 15, 13);
    vv->setSpacing(3);
    verdict_head_ = new QLabel;
    verdict_head_->setObjectName("h2");
    verdict_head_->setWordWrap(true);
    verdict_sub_ = make_label("", "note");
    vv->addWidget(verdict_head_);
    vv->addWidget(verdict_sub_);
    v->addWidget(verdict_panel_);

    auto *stats = new QHBoxLayout;
    stats->setSpacing(1);
    const char *keys[5] = {"RAMP STOPS AT", "FINAL VOLUME", "FILL", "SQUEEZED OUT",
                           "HIGHEST SAFE FLOOR"};
    for (int i = 0; i < 5; ++i) {
        auto *tile = make_panel(i == 4 ? "sunk" : "panel");
        auto *tv = new QVBoxLayout(tile);
        tv->setContentsMargins(12, 9, 12, 9);
        tv->setSpacing(2);
        auto *k = new QLabel(keys[i]);
        k->setObjectName("statKey");
        stat_value_[i] = new QLabel("—");
        stat_value_[i]->setObjectName("statValue");
        tv->addWidget(k);
        tv->addWidget(stat_value_[i]);
        stat_tile_[i] = tile;
        stats->addWidget(tile);
    }
    v->addLayout(stats);

    auto *chart_panel = make_panel();
    auto *cv = new QVBoxLayout(chart_panel);
    cv->setContentsMargins(15, 13, 15, 13);
    cv->setSpacing(8);
    cv->addWidget(make_label("Volume against mass, one point per ramp pass", "h2"));
    ramp_chart_ = new RampChart;
    cv->addWidget(ramp_chart_);
    v->addWidget(chart_panel);

    auto *visual = make_panel();
    auto *xv = new QHBoxLayout(visual);
    xv->setContentsMargins(15, 13, 15, 13);
    xv->setSpacing(14);
    diagram_ = new BowlDiagram;
    xv->addWidget(diagram_, 3);
    for (int i = 0; i < 2; ++i) {
        auto *col = new QVBoxLayout;
        col->setSpacing(4);
        photo_[i] = new QLabel;
        photo_[i]->setAlignment(Qt::AlignCenter);
        photo_[i]->setMinimumSize(120, 160);
        photo_caption_[i] = make_label("", "note");
        photo_caption_[i]->setAlignment(Qt::AlignCenter);
        col->addWidget(photo_[i], 1);
        col->addWidget(photo_caption_[i]);
        xv->addLayout(col, 1);
    }
    v->addWidget(visual);

    auto *break_panel = make_panel();
    auto *bv = new QVBoxLayout(break_panel);
    bv->setContentsMargins(15, 13, 15, 13);
    bv->setSpacing(8);
    bv->addWidget(make_label("Where the volume goes", "h2"));
    breakdown_ = new QTableWidget(0, 7);
    breakdown_->setHorizontalHeaderLabels({"ingredient", "oz/+100g at end", "start g",
                                           "final g", "added g", "added oz", "share"});
    breakdown_->verticalHeader()->setVisible(false);
    breakdown_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    breakdown_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    breakdown_->setMinimumHeight(190);
    bv->addWidget(breakdown_);
    foot_note_ = make_label("", "note");
    bv->addWidget(foot_note_);
    v->addWidget(break_panel);

    auto *curve_panel = make_panel();
    auto *qv = new QVBoxLayout(curve_panel);
    qv->setContentsMargins(15, 13, 15, 13);
    qv->setSpacing(8);
    qv->addWidget(make_label("The measured curve", "h2"));
    curve_chart_ = new CurveChart;
    qv->addWidget(curve_chart_);
    v->addWidget(curve_panel);

    v->addStretch(1);
    scroll->setWidget(host);
    return scroll;
}

void MainWindow::reload_theme()
{
    qApp->setStyleSheet(theme::stylesheet());
    for (int i = 0; i < 2; ++i)
        if (base_fit_[i]) base_fit_[i]->setFont(theme::mono(8));
    update();
}

void MainWindow::toggle_theme()
{
    theme::set_dark(!theme::is_dark());
    reload_theme();
    recompute();
}


//
// ############################################################################
// Model wiring
//

const Menu *MainWindow::menu() const
{
    const int i = brand_->currentIndex();
    return (i >= 0 && i < static_cast<int>(menus_.size())) ? &menus_[i] : nullptr;
}

Method MainWindow::method() const
{
    if (method_hand_->isChecked()) return Method::Hand;
    if (method_pooled_->isChecked()) return Method::Pooled;
    return Method::Robot;
}

double MainWindow::value_for(const QString &name, const QString &fieldName) const
{
    auto it = overrides_.find(name);
    if (it != overrides_.end()) {
        const Override &o = it->second;
        if (fieldName == "start" && o.start_g) return *o.start_g;
        if (fieldName == "step" && o.step_g) return *o.step_g;
        if (fieldName == "max" && o.max_g) return *o.max_g;
        if (fieldName == "rate" && o.rate) return *o.rate;
    }
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(name) : nullptr;
    if (!ing) return 0.0;
    if (fieldName == "start") return ing->full_portion();
    if (fieldName == "step") return ing->step_increment_g;
    if (fieldName == "max") return ing->max_dispense_weight_g;
    return ing->kind == Kind::Protein ? kFlatProteinRate : kFlatToppingRate;
}

QStringList MainWindow::bowl_names() const
{
    QStringList out;
    if (!base1_->currentData().toString().isEmpty()) out << base1_->currentData().toString();
    if (!base2_->currentData().toString().isEmpty()) out << base2_->currentData().toString();
    out << picked_proteins_ << picked_toppings_;
    return out;
}

std::vector<BowlItem> MainWindow::assemble_bowl() const
{
    std::vector<BowlItem> items;
    const Menu *m = menu();
    if (!m) return items;

    // Dispense order is stacking order: bases, then proteins, then toppings.
    for (int i = 0; i < 2; ++i) {
        const QString name =
            (i == 0 ? base1_ : base2_)->currentData().toString();
        if (name.isEmpty()) continue;
        const Ingredient *ing = m->find(name);
        if (!ing) continue;
        BowlItem it = make_item(*ing, curves_, method(), kFlatProteinRate, kFlatToppingRate);
        it.start_g = base_start_[i]->value();
        it.step_g = base_step_[i]->value();
        it.max_g = base_max_[i]->value();
        items.push_back(it);
    }
    for (const QStringList *group : {&picked_proteins_, &picked_toppings_}) {
        for (const QString &name : *group) {
            const Ingredient *ing = m->find(name);
            if (!ing) continue;
            BowlItem it =
                make_item(*ing, curves_, method(), kFlatProteinRate, kFlatToppingRate);
            it.start_g = value_for(name, "start");
            it.step_g = value_for(name, "step");
            it.max_g = value_for(name, "max");
            it.flat_oz_per_100g = value_for(name, "rate");
            items.push_back(it);
        }
    }
    return items;
}

//
// ############################################################################
// Population
//

void MainWindow::populate_brand()
{
    loading_ = true;
    brand_->clear();
    for (const Menu &m : menus_) brand_->addItem(m.brand, m.brand);
    loading_ = false;
    if (!menus_.empty()) on_brand_changed();
}

void MainWindow::on_brand_changed()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;

    loading_ = true;
    overrides_.clear();
    picked_proteins_.clear();
    picked_toppings_.clear();

    for (int i = 0; i < 2; ++i) {
        QComboBox *box = (i == 0 ? base1_ : base2_);
        box->clear();
        if (i == 1) box->addItem("— none —", QString());
        for (const QString &n : m->names_of(Kind::Base)) box->addItem(n, n);
    }
    const QStringList bases = m->names_of(Kind::Base);
    if (!bases.isEmpty()) base1_->setCurrentIndex(0);
    base2_->setCurrentIndex(0);

    recipe_->clear();
    recipe_->addItem("— build your own —", QString());
    for (const Recipe &r : m->recipes) recipe_->addItem(r.name, r.name);
    recipe_->setCurrentIndex(0);

    const QStringList proteins = m->names_of(Kind::Protein);
    if (!proteins.isEmpty()) picked_proteins_ << proteins.front();

    brand_note_->setText(
        m->floors.empty()
            ? QString("<span style='color:%1'>Menu <code>%2</code> has no "
                      "<code>dynamic_portion_increases</code> and no product rules at "
                      "all.</span> Nothing ramps and no base is ever split on this "
                      "brand.")
                  .arg(theme::palette().over.name(), m->version)
            : QString("Menu <code>%1</code> — <b>%2</b> ingredients carry ramp "
                      "settings, <b>%3</b> weight floors. Base-split rule %4.")
                  .arg(m->version)
                  .arg(m->ingredients.size())
                  .arg(m->floors.size())
                  .arg(m->has_split_rule
                           ? QString("covers <b>%1</b> menu items").arg(m->split_rule_items)
                           : QString("is absent")));

    loading_ = false;
    rebuild_ingredient_pickers();
    on_recipe_changed();
}

void MainWindow::on_recipe_changed()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;

    loading_ = true;
    overrides_.clear();
    picked_proteins_.clear();
    picked_toppings_.clear();

    const QString want = recipe_->currentData().toString();
    const Recipe *rec = nullptr;
    for (const Recipe &r : m->recipes)
        if (r.name == want) rec = &r;

    if (!rec) {
        const QStringList bases = m->names_of(Kind::Base);
        base1_->setCurrentIndex(bases.isEmpty() ? -1 : 0);
        base2_->setCurrentIndex(0);
        const QStringList proteins = m->names_of(Kind::Protein);
        if (!proteins.isEmpty()) picked_proteins_ << proteins.front();
        recipe_note_->setText(
            m->recipes.empty()
                ? QString("<span style='color:%1'>This menu defines no preconfigured "
                          "bowls.</span>").arg(theme::palette().over.name())
                : "Nothing preset — pick the bases, proteins and toppings yourself, as a "
                  "customer would.");
    } else {
        QStringList rec_bases;
        for (const RecipeItem &ri : rec->items) {
            const Ingredient *ing = m->find(ri.name);
            if (!ing) continue;
            overrides_[ri.name].start_g = ri.grams;
            if (ing->kind == Kind::Base) rec_bases << ri.name;
            else if (ing->kind == Kind::Protein) picked_proteins_ << ri.name;
            else picked_toppings_ << ri.name;
        }
        auto select = [](QComboBox *box, const QString &name) {
            const int i = box->findData(name);
            box->setCurrentIndex(i >= 0 ? i : 0);
        };
        select(base1_, rec_bases.value(0));
        select(base2_, rec_bases.value(1));

        const int n = static_cast<int>(rec->items.size());
        QString note = QString("Pins <b>%1</b> ingredient%2")
                           .arg(n)
                           .arg(n == 1 ? "" : "s");
        note += n <= 2 ? " — this template only fixes the protein, so the rest of the "
                         "bowl is still customer-chosen."
                       : ".";
        if (rec->unweighted() > 0)
            note += QString(" <span style='color:%1'>%2 carried no weight in the menu "
                            "and fall back to that ingredient's normal portion.</span>")
                        .arg(theme::palette().over.name())
                        .arg(rec->unweighted());
        recipe_note_->setText(note);
    }

    loading_ = false;
    on_selection_changed();
}

void MainWindow::rebuild_ingredient_pickers()
{
    const Menu *m = menu();
    if (!m) return;

    for (auto [host, kind, picked] :
         {std::tuple{protein_picks_, Kind::Protein, &picked_proteins_},
          std::tuple{topping_picks_, Kind::Topping, &picked_toppings_}}) {
        auto *grid = qobject_cast<QGridLayout *>(host->layout());
        while (QLayoutItem *item = grid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QStringList names = m->names_of(kind);
        if (names.isEmpty()) {
            grid->addWidget(make_label(QString("This menu configures no %1s.")
                                           .arg(to_string(kind)), "note"), 0, 0);
            continue;
        }
        int row = 0, col = 0;
        for (const QString &n : names) {
            auto *cb = new QCheckBox(n);
            cb->setChecked(picked->contains(n));
            connect(cb, &QCheckBox::toggled, this, [this, kind, n](bool on) {
                QStringList &list =
                    kind == Kind::Protein ? picked_proteins_ : picked_toppings_;
                if (on && !list.contains(n)) list << n;
                if (!on) list.removeAll(n);
                on_selection_changed();
            });
            grid->addWidget(cb, row, col);
            if (++col == 2) { col = 0; ++row; }
        }
    }
}

void MainWindow::rebuild_override_rows()
{
    const Menu *m = menu();
    if (!m) return;
    QStringList rows = picked_proteins_ + picked_toppings_;

    override_table_->setRowCount(rows.size());
    override_table_->setVisible(!rows.isEmpty());
    for (int r = 0; r < rows.size(); ++r) {
        const QString name = rows[r];
        const Ingredient *ing = m->find(name);
        auto *label = new QTableWidgetItem(name);
        if (ing && (ing->source == WeightSource::Class || ing->source == WeightSource::None))
            label->setForeground(theme::palette().over);
        label->setToolTip(ing ? QString("portion source: %1").arg(to_string(ing->source))
                              : QString());
        override_table_->setItem(r, 0, label);

        const char *fields[4] = {"start", "step", "max", "rate"};
        for (int c = 0; c < 4; ++c) {
            auto *spin = make_spin(0, 2000, c == 1 ? 0.1 : 1, c == 1 || c == 3 ? 2 : 0);
            spin->setValue(value_for(name, fields[c]));
            const QString f = fields[c];
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, name, f](double v) {
                Override &o = overrides_[name];
                if (f == "start") o.start_g = v;
                else if (f == "step") o.step_g = v;
                else if (f == "max") o.max_g = v;
                else o.rate = v;
                recompute();
            });
            override_table_->setCellWidget(r, c + 1, spin);
        }
    }
    override_table_->setMaximumHeight(
        std::min(260, 30 + static_cast<int>(rows.size()) * 34));
}

void MainWindow::apply_floor()
{
    const Menu *m = menu();
    if (!m) return;
    const FloorRule *f = m->floor_for(bowl_names());

    const QSignalBlocker block(floor_);
    floor_->setValue(f ? f->minimum_product_weight_g : 0.0);

    if (m->floors.empty()) {
        floor_note_->setText("This menu has no floors — the ramp never fires.");
    } else if (!f) {
        floor_note_->setText(QString("<span style='color:%1'>No floor rule matches this "
                                     "bowl</span>, so the algorithm does nothing.")
                                 .arg(theme::palette().over.name()));
    } else {
        QString on = f->must_have.isEmpty()
                         ? QString("anything")
                         : QString("<b>%1</b>").arg(f->must_have.join("</b>, <b>"));
        QString text = QString("Matched <code>%1</code> on %2")
                           .arg(f->op == FilterOp::ExactlyOne ? "ExactlyOne" : "Any", on);
        if (!f->must_not_have.isEmpty())
            text += QString(", excluding %1").arg(f->must_not_have.join(", "));
        text += QString(" → <b>%1 g</b>. First matching rule wins; edit the field to "
                        "override.").arg(f->minimum_product_weight_g);
        floor_note_->setText(text);
    }
}

void MainWindow::on_selection_changed()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;

    loading_ = true;
    // A preconfigured bowl can carry three proteins (Siren counts Broccoli as one),
    // so the cap floats up to whatever is already selected rather than locking the
    // recipe out.
    const int cap = std::max(2, static_cast<int>(picked_proteins_.size()));
    if (auto *grid = qobject_cast<QGridLayout *>(protein_picks_->layout())) {
        for (int i = 0; i < grid->count(); ++i)
            if (auto *cb = qobject_cast<QCheckBox *>(grid->itemAt(i)->widget())) {
                cb->setChecked(picked_proteins_.contains(cb->text()));
                cb->setEnabled(cb->isChecked() || picked_proteins_.size() < cap);
            }
    }
    if (auto *grid = qobject_cast<QGridLayout *>(topping_picks_->layout()))
        for (int i = 0; i < grid->count(); ++i)
            if (auto *cb = qobject_cast<QCheckBox *>(grid->itemAt(i)->widget()))
                cb->setChecked(picked_toppings_.contains(cb->text()));

    const bool two_bases = !base2_->currentData().toString().isEmpty();
    for (int i = 0; i < 2; ++i) {
        const QString name = (i == 0 ? base1_ : base2_)->currentData().toString();
        base_card_[i]->setVisible(!name.isEmpty());
        if (name.isEmpty()) continue;
        const Ingredient *ing = m->find(name);
        if (!ing) continue;

        const QSignalBlocker b1(base_start_[i]), b2(base_step_[i]), b3(base_max_[i]);
        auto ov = overrides_.find(name);
        const bool pinned = ov != overrides_.end() && ov->second.start_g;
        base_start_[i]->setValue(pinned ? *ov->second.start_g
                                        : split_base_start(*ing, split_->isChecked(),
                                                           two_bases));
        base_step_[i]->setValue(ing->step_increment_g);
        base_max_[i]->setValue(ing->max_dispense_weight_g);
    }

    split_note_->setText(
        split_->isChecked()
            ? "<code>ProportionalReductionForPortionTarget</code> halves each base then "
              "floors at <code>per_portion_minimum_weight_g</code>. Applies when two "
              "bases are selected."
            : "Bases start at their <b>full</b> configured portion. Turn this on to "
              "model the 50:50 split — it only fires for menu items listed in the rule, "
              "which is why so many double-base bowls run unreduced.");

    compress_note_->setText(
        compress_->isChecked()
            ? QString("Each base is squeezed by whatever is dispensed after it. The "
                      "squeeze comes from the same exponent <code>p</code> the curves "
                      "were fitted with. <span style='color:%1'>No loaded bowls were "
                      "measured — this is an extrapolation.</span>")
                  .arg(theme::palette().over.name())
            : "The measured bowls held one ingredient and nothing on top, so these "
              "volumes are unloaded. Turn this on to model the protein pressing down.");
    load_transfer_->setVisible(compress_->isChecked());

    const QString ms = to_string(method());
    method_note_->setText(
        ms == "robot" ? "Bowls the <b>robot</b> dispensed — the operative case, and the "
                        "most compacted."
        : ms == "hand" ? "<b>Hand</b>-filled reference bowls. Looser packing: kale reads "
                         "+13% at equal mass (p = 0.001), romaine +6%, rice unchanged."
                       : "Both methods <b>pooled</b>. Avoid for kale — its two arms "
                         "differ significantly, so pooling averages two relationships.");

    loading_ = false;
    rebuild_override_rows();
    apply_floor();
    recompute();
}


//
// ############################################################################
// Rendering
//

void MainWindow::recompute()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;

    SimSettings settings;
    settings.floor_g = floor_->value();
    settings.bowl_capacity_oz = capacity_->value();
    settings.compress = compress_->isChecked();
    settings.load_transfer = load_transfer_->value();

    last_ = simulate(assemble_bowl(), settings);
    const SimResult &r = last_;
    const theme::Palette &pal = theme::palette();
    const double cap = settings.bowl_capacity_oz;
    const Frame &last = r.last();

    // ---- verdict ----------------------------------------------------------
    QString head, sub;
    QColor accent_border = pal.rule_strong, accent_fill = pal.panel_sunk,
           head_colour = pal.ink;
    const double shortfall = settings.floor_g - last.grams;

    switch (r.verdict) {
    case Verdict::NoFloor:
        head = r.crossed_at_g ? "No floor is set, and the bowl is already over"
                              : "No weight floor applies — the ramp never fires";
        sub = (m->floors.empty()
                   ? "This menu carries no dynamic_portion_increases at all, so nothing "
                     "ever ramps. "
                   : "No filter in this menu matches this combination of ingredients, so "
                     "the algorithm returns without touching the bowl. ")
              + QString("As ordered it is %1 oz at %2 g, %3% of the bowl.")
                    .arg(fmt(last.ounces)).arg(std::round(last.grams))
                    .arg(std::round(last.ounces / cap * 100));
        if (r.crossed_at_g) { accent_border = pal.over; accent_fill = pal.over_soft;
                              head_colour = pal.over; }
        break;
    case Verdict::FitsMassBound:
        head = "Mass limit binds first. The bowl fits.";
        sub = QString("Clears %1 g at %2 g and %3 oz — %4 oz under the %5 oz limit.")
                  .arg(std::round(settings.floor_g)).arg(std::round(last.grams))
                  .arg(fmt(last.ounces)).arg(fmt(cap - last.ounces))
                  .arg(fmt(cap, cap < 10 ? 1 : 0));
        accent_border = pal.accent; accent_fill = pal.accent_soft; head_colour = pal.accent;
        break;
    case Verdict::Saturated:
        head = "Neither limit reached — the ingredients saturate first";
        sub = QString("Everything is at its max%1 the %2 g floor, at %3 oz with %4 oz "
                      "spare.")
                  .arg(shortfall >= 1 ? QString(" %1 g short of").arg(std::round(shortfall))
                                      : QString(", landing on"))
                  .arg(std::round(settings.floor_g)).arg(fmt(last.ounces))
                  .arg(fmt(cap - last.ounces));
        break;
    case Verdict::OverAtStart:
        head = "Over the bowl before the ramp starts";
        sub = QString("The ordered portions alone come to %1 oz at %2 g — past %3 oz "
                      "with no ramping at all. No weight floor can fix this; the "
                      "portions themselves have to come down.")
                  .arg(fmt(r.first().ounces)).arg(std::round(r.first().grams))
                  .arg(fmt(cap, cap < 10 ? 1 : 0));
        accent_border = pal.over; accent_fill = pal.over_soft; head_colour = pal.over;
        break;
    case Verdict::OverWhileRamping:
        head = "Volume limit binds first";
        sub = QString("The bowl passes %1 oz at %2 g, %3 g before the ramp stops at "
                      "%4 g. A floor near %5 would keep it inside the bowl.")
                  .arg(fmt(cap, cap < 10 ? 1 : 0))
                  .arg(std::round(*r.crossed_at_g))
                  .arg(std::round(last.grams - *r.crossed_at_g))
                  .arg(std::round(last.grams))
                  .arg(r.highest_safe_floor_g
                           ? QString("%1 g").arg(std::round(*r.highest_safe_floor_g))
                           : QString("below the starting weight"));
        accent_border = pal.over; accent_fill = pal.over_soft; head_colour = pal.over;
        break;
    }
    verdict_head_->setText(head);
    verdict_head_->setStyleSheet(QString("color:%1;").arg(head_colour.name()));
    verdict_sub_->setText(sub);
    verdict_panel_->setStyleSheet(
        QString("QFrame#panel{background:%1;border:1px solid %2;border-radius:10px;}")
            .arg(accent_fill.name(), accent_border.name()));

    // ---- stat tiles -------------------------------------------------------
    stat_value_[0]->setText(QString("%1 g").arg(std::round(last.grams)));
    stat_value_[1]->setText(QString("%1 oz").arg(fmt(last.ounces)));
    stat_value_[2]->setText(QString("%1 %").arg(std::round(last.ounces / cap * 100)));
    stat_value_[3]->setText(QString("%1 oz").arg(fmt(r.squeezed_oz())));
    stat_tile_[3]->setVisible(settings.compress);
    stat_value_[4]->setText(
        r.crossed_at_g ? (r.highest_safe_floor_g
                              ? QString("%1 g").arg(std::round(*r.highest_safe_floor_g))
                              : QString("none"))
                       : QString("no limit"));
    stat_value_[4]->setStyleSheet(QString("color:%1;").arg(pal.mass.name()));

    // ---- breakdown --------------------------------------------------------
    double total_oz = 0;
    for (double oz : last.per_item_oz) total_oz += oz;
    if (total_oz <= 0) total_oz = 1;

    breakdown_->setRowCount(static_cast<int>(r.items.size()));
    for (size_t i = 0; i < r.items.size(); ++i) {
        const BowlItem &it = r.items[i];
        const double added_g = it.final_g - it.start_g;
        const double added_oz = last.per_item_oz[i] - r.first().per_item_oz[i];
        const double squeeze =
            last.per_item_oz_uncompressed[i] - last.per_item_oz[i];

        QString name = it.name;
        if (it.kind != Kind::Base) name += QString("  (%1)").arg(to_string(it.kind));
        if (settings.compress && squeeze > 0.05)
            name += QString("  −%1 oz").arg(fmt(squeeze));
        auto *cell = new QTableWidgetItem(name);
        if (it.kind == Kind::Base) cell->setForeground(theme::series_colour(it.name));
        const QString cols[6] = {
            fmt(it.marginal_oz_per_100g(it.final_g), 2),
            QString::number(std::round(it.start_g)),
            QString::number(std::round(it.final_g)),
            added_g > 0.05 ? QString("+%1").arg(std::round(added_g)) : "—",
            added_oz > 0.05 ? QString("+%1").arg(fmt(added_oz)) : "—",
            QString("%1%").arg(std::round(last.per_item_oz[i] / total_oz * 100)),
        };
        breakdown_->setItem(static_cast<int>(i), 0, cell);
        for (int c = 0; c < 6; ++c) {
            auto *v = new QTableWidgetItem(cols[c]);
            v->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            breakdown_->setItem(static_cast<int>(i), c + 1, v);
        }
    }
    breakdown_->resizeColumnsToContents();
    breakdown_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    // ---- footnote ---------------------------------------------------------
    std::vector<const BowlItem *> ramped;
    for (size_t i = 0; i < r.items.size(); ++i)
        if (r.items[i].final_g - r.items[i].start_g > 0.05) ramped.push_back(&r.items[i]);

    const QString scaling =
        QString("<b>%1</b> ingredient%2 ramp together at <b>%3 g</b> per pass, of which "
                "the base%4 <b>%5%</b>. Add another topping and the floor arrives in "
                "fewer passes, so the greens are asked for less of it.")
            .arg(r.items.size())
            .arg(r.items.size() == 1 ? "" : "s")
            .arg(fmt(r.grams_per_pass()))
            .arg(std::count_if(r.items.begin(), r.items.end(),
                               [](const BowlItem &i) { return i.kind == Kind::Base; }) == 1
                     ? " supplies" : "s supply")
            .arg(std::round(r.base_share_of_step() * 100));

    if (ramped.size() > 1) {
        auto cost = [&](const BowlItem *i) { return i->marginal_oz_per_100g(i->final_g); };
        const BowlItem *worst = *std::max_element(ramped.begin(), ramped.end(),
                                   [&](auto a, auto b) { return cost(a) < cost(b); });
        const BowlItem *best = *std::min_element(ramped.begin(), ramped.end(),
                                   [&](auto a, auto b) { return cost(a) < cost(b); });
        double oz_per_pass = 0;
        for (size_t i = 0; i < r.items.size(); ++i)
            oz_per_pass += last.per_item_oz[i] - r.first().per_item_oz[i];
        oz_per_pass /= std::max(1, r.passes);

        const double ratio = cost(best) > 0 ? cost(worst) / cost(best) : 1.0;
        foot_note_->setText(
            (ratio > 1.15
                 ? QString("At the mass the ramp ends on, <b>%1</b> costs <b>%2×</b> the "
                           "volume per added gram that %3 does (%4 vs %5 oz per +100 g). ")
                       .arg(worst->name).arg(fmt(ratio, 2)).arg(best->name)
                       .arg(fmt(cost(worst))).arg(fmt(cost(best)))
                 : QString("At the mass the ramp ends on, everything that ramps costs "
                           "about the same per added gram (%1 vs %2 oz per +100 g). ")
                       .arg(fmt(cost(best))).arg(fmt(cost(worst))))
            + scaling
            + QString(" Each pass costs %1 oz of bowl.").arg(fmt(oz_per_pass, 2)));
    } else if (ramped.size() == 1) {
        foot_note_->setText(QString("Only <b>%1</b> ramps: %2 oz of bowl spent to gain "
                                    "%3 g, over %4 passes.")
                                .arg(ramped[0]->name)
                                .arg(fmt(last.per_item_oz[0] - r.first().per_item_oz[0]))
                                .arg(std::round(ramped[0]->final_g - ramped[0]->start_g))
                                .arg(r.passes));
    } else {
        foot_note_->setText("Nothing ramps — the bowl already clears its floor, this "
                            "menu sets no floor, or everything starts at its max.");
    }

    // ---- base fit labels --------------------------------------------------
    for (int i = 0; i < 2; ++i) {
        const QString name = (i == 0 ? base1_ : base2_)->currentData().toString();
        if (name.isEmpty()) continue;
        const Ingredient *ing = m->find(name);
        if (!ing) continue;
        const QString proxy = proxy_curve_for(name, curves_);
        const Fit *f = curves_.fit(proxy.isEmpty() ? name : proxy, method());
        QString text;
        if (f) {
            const double mid = (base_start_[i]->value() + base_max_[i]->value()) / 2;
            text = QString("oz = %1·g^%2 · <b>%3</b> oz per +100 g at %4 g<br>")
                       .arg(fmt(f->K, 3)).arg(fmt(f->p, 3))
                       .arg(fmt(f->marginal_oz_per_100g(mid))).arg(std::round(mid));
            text += QString("<b>menu</b> portion %1 g, min %2 g<br>")
                        .arg(ing->weights.empty()
                                 ? QString("—")
                                 : [&] { QStringList w; for (double x : ing->weights)
                                             w << QString::number(x, 'f', 0);
                                         return w.join("/"); }())
                        .arg(ing->per_portion_minimum_weight_g, 0, 'f', 0);
            text += QString("<b>fit</b> R² %1, n=%2, measured %3–%4 g")
                        .arg(fmt(f->r2, 2)).arg(f->n)
                        .arg(std::round(f->lo_g)).arg(std::round(f->hi_g));
            if (!proxy.isEmpty())
                text += QString(" · <span style='color:%1'>no data — using %2</span>")
                            .arg(pal.over.name(), proxy);
            if (base_start_[i]->value() < f->lo_g * 0.8
                || base_max_[i]->value() > f->hi_g * 1.25)
                text += QString(" · <span style='color:%1'>ramp runs outside measured "
                                "range</span>").arg(pal.over.name());
        } else {
            text = QString("<span style='color:%1'>No measured curve for %2 — it is "
                           "treated as a flat rate.</span>").arg(pal.over.name(), name);
        }
        base_fit_[i]->setText(text);
    }

    // ---- photos -----------------------------------------------------------
    static const std::map<QString, QString> kPhotoPrefix = {
        {"Romaine Base", "Romaine"}, {"Massaged Kale", "Kale"},
        {"Mexican Rice", "MexicanRice"}, {"White Rice", "MexicanRice"},
        {"Brown Rice and Lentils", "MexicanRice"}};
    QDir photo_dir(asset_dir_ + "/photos");
    int slot = 0;
    for (const BowlItem &it : r.items) {
        if (it.kind != Kind::Base || slot > 1) continue;
        auto pit = kPhotoPrefix.find(it.name);
        if (pit == kPhotoPrefix.end()) continue;
        // Photos exist at fixed masses; show the nearest one to the simulated weight.
        double best = -1, best_d = 1e18;
        for (const QString &f : photo_dir.entryList({pit->second + "_*g.jpg"}, QDir::Files)) {
            const double g = QStringView(f).mid(pit->second.size() + 1,
                                                f.size() - pit->second.size() - 6).toDouble();
            if (std::fabs(g - it.final_g) < best_d) { best_d = std::fabs(g - it.final_g); best = g; }
        }
        if (best < 0) continue;
        QPixmap pm(photo_dir.filePath(QString("%1_%2g.jpg").arg(pit->second)
                                          .arg(best, 0, 'f', 0)));
        if (pm.isNull()) continue;
        photo_[slot]->setPixmap(pm.scaled(photo_[slot]->width(), photo_[slot]->height(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
        photo_caption_[slot]->setText(
            QString("<b>%1</b><br>%2 g simulated · %3 g photo%4")
                .arg(it.name).arg(std::round(it.final_g)).arg(best, 0, 'f', 0)
                .arg(pit->second == "MexicanRice" && it.name != "Mexican Rice"
                         ? "<br><span style='color:" + pal.over.name()
                               + "'>proxy series</span>"
                         : ""));
        ++slot;
    }
    for (int i = slot; i < 2; ++i) {
        photo_[i]->clear();
        photo_caption_[i]->setText(i == 0 ? "No measured photo series for this base." : "");
    }

    ramp_chart_->set_show_uncompressed(settings.compress);
    ramp_chart_->set_result(r);
    diagram_->set_result(r);
    curve_chart_->set_curves(&curves_, method());

    QStringList bits;
    for (const QString &n : curves_.ingredients())
        bits << QString("%1 (%2)").arg(n).arg(curves_.count(n, Method::Pooled));
    curve_note_->setText(QString("<b>%1</b> measurements across %2 ingredient%3: %4.")
                             .arg(curves_.observations().size())
                             .arg(curves_.ingredients().size())
                             .arg(curves_.ingredients().size() == 1 ? "" : "s")
                             .arg(bits.join(", ")));
}

//
// ############################################################################
// CSV ingest
//

void MainWindow::show_csv_help()
{
    QMessageBox box(this);
    box.setWindowTitle("Mass-to-volume CSV format");
    box.setTextFormat(Qt::RichText);
    box.setText(CurveSet::format_help());
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::on_upload_csv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Add mass-to-volume measurements", asset_dir_ + "/data",
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QString note;
    int added = 0;
    QStringList fresh;
    if (!curves_.load_csv(path, &note, &added, &fresh)) {
        QMessageBox box(this);
        box.setWindowTitle("Could not read that CSV");
        box.setIcon(QMessageBox::Warning);
        box.setText(note);
        box.setInformativeText("Click Show Details for the expected format.");
        box.setDetailedText(QString(CurveSet::format_help())
                                .replace(QRegularExpression("<[^>]+>"), ""));
        box.exec();
        return;
    }

    QString msg = QString("Added %1 row%2. ").arg(added).arg(added == 1 ? "" : "s");
    if (!fresh.isEmpty())
        msg += QString("New curve%1 for %2. ")
                   .arg(fresh.size() == 1 ? "" : "s", fresh.join(", "));
    if (!note.isEmpty()) msg += note;
    QMessageBox::information(this, "Measurements added", msg);

    recompute();
}

void MainWindow::on_reset_curves()
{
    curves_.reset_to_builtin(asset_dir_ + "/data/mass_to_volume.csv");
    recompute();
}

}  // namespace bowlfill
