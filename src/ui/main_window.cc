#include "main_window.hh"

#include "core/bowl_data.hh"
#include "core/measurements.hh"

#include "bowl_diagram.hh"
#include "curve_chart.hh"
#include "ramp_chart.hh"
#include "tolerance_chart.hh"
#include "mapping_dialog.hh"
#include "theme.hh"
#include "units.hh"

#include <QApplication>
#include <QBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <functional>
#include <map>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include "report_dialog.hh"
#include "sweep_dialog.hh"
#include "core/feasibility.hh"
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <algorithm>
#include <numeric>
#include <cmath>

namespace bowlfill {
namespace {

const char *const kIntroText =
    "The dynamic portion algorithm optimises on <i>mass</i>: it steps every "
    "ingredient until the bowl clears its recipe's gram floor. Bowls fail at the "
    "lidder on <i>volume</i>. Ramp settings, portion weights and floors are read "
    "from the menu dumps in <code>menus/</code>; mass→volume is "
    "<code>%1 = K·g^p</code>, fitted to the measured bowls.";

namespace space = theme::space;

/// Every input and button is this tall, so rows of mixed controls line up.
constexpr int kControlHeight = 32;

/// Space between a scroll area's edge and the panels inside it; see build_controls().
constexpr int kEdgeInset = 2;

QLabel *make_label(const QString &text, const char *role)
{
    auto *l = new QLabel(text);
    l->setObjectName(role);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

/// A small uppercase label that heads a group of controls inside a panel.
QLabel *section_label(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("section");
    return l;
}

QFrame *make_panel(const char *role = "panel")
{
    auto *f = new QFrame;
    f->setObjectName(role);
    return f;
}

/// A panel with a heading, returning the layout its content goes into. Every panel
/// in the window uses the same padding and heading, which is most of what makes the
/// page read as one system.
QFrame *titled_panel(const QString &title, QVBoxLayout *&body)
{
    QFrame *panel = make_panel();
    body = new QVBoxLayout(panel);
    body->setContentsMargins(space::lg, space::lg, space::lg, space::lg);
    body->setSpacing(space::md);
    if (!title.isEmpty()) body->addWidget(make_label(title, "heading"));
    return panel;
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
    s->setFixedHeight(kControlHeight);
    // A spin box asks for room to show its whole range; in a row of four that is
    // wider than the sidebar. Let the layout decide the width instead.
    s->setMinimumWidth(48);
    s->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return s;
}

QWidget *field(const QString &caption, QWidget *w)
{
    auto *box = new QWidget;
    box->setObjectName("clear");
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(space::xs);
    auto *l = new QLabel(caption);
    l->setObjectName("label");
    v->addWidget(l);
    v->addWidget(w);
    return box;
}

/// Lays several fields out side by side at equal widths.
QHBoxLayout *field_row(std::initializer_list<QWidget *> fields)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(space::sm);
    for (QWidget *f : fields) row->addWidget(f, 1);
    return row;
}

QPushButton *make_toggle(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

/// A segmented control: mutually exclusive options sharing one outline.
QFrame *segment(std::initializer_list<QPushButton *> buttons)
{
    auto *frame = new QFrame;
    frame->setObjectName("segment");
    auto *h = new QHBoxLayout(frame);
    h->setContentsMargins(2, 2, 2, 2);
    h->setSpacing(2);
    for (QPushButton *b : buttons) h->addWidget(b, 1);
    frame->setFixedHeight(kControlHeight + 2);
    return frame;
}

/// A combo box that never asks for more width than its column gives it. By default
/// a combo is as wide as its longest entry, and two side by side ("Brown Rice and
/// Lentils") overflow the sidebar; long names are elided instead.
QComboBox *make_combo()
{
    auto *c = new QComboBox;
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(6);
    c->setFixedHeight(kControlHeight);
    return c;
}

QString fmt(double v, int dp = 1) { return QString::number(v, 'f', dp); }

struct BreakdownRow {
    QString name, kind;
    QColor swatch;              ///< the ingredient's band in the bowl diagram
    QColor colour;              ///< text colour; invalid means the default ink
    std::vector<QString> cells; ///< the numeric columns, right-aligned
};

QIcon swatch_icon(const QColor &colour)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(14, 14) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    if (colour.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
    }
    return QIcon(pm);
}

///
/// Fills the "where the volume goes" table, which doubles as the bowl diagram's
/// legend: each name carries the swatch of its band. The table is sized to its rows
/// so the page scrolls as one, rather than the table scrolling inside the page.
///
void fill_breakdown(QTableWidget *table, const QStringList &headers,
                    const std::map<int, QString> &header_tips,
                    const std::vector<BreakdownRow> &rows)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(static_cast<int>(rows.size()));
    table->setIconSize(QSize(14, 14));
    for (int c = 0; c < headers.size(); ++c)
        if (QTableWidgetItem *h = table->horizontalHeaderItem(c))
            h->setTextAlignment((c == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
    for (const auto &[c, tip] : header_tips)
        if (QTableWidgetItem *h = table->horizontalHeaderItem(c)) h->setToolTip(tip);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const BreakdownRow &row = rows[i];
        auto *name = new QTableWidgetItem(swatch_icon(row.swatch), row.name);
        name->setToolTip(row.kind);
        if (row.colour.isValid()) name->setForeground(row.colour);
        table->setItem(i, 0, name);
        for (size_t c = 0; c < row.cells.size(); ++c) {
            auto *v = new QTableWidgetItem(row.cells[c]);
            v->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(i, static_cast<int>(c) + 1, v);
        }
    }
    // Numeric columns are sized by hand: resizeColumnsToContents() reserves room in
    // each header for a sort arrow the table never shows, and in a narrow window that
    // slack is taken from the ingredient names.
    const QFontMetrics body(theme::font(theme::Text::Body));
    const QFontMetrics head(theme::font(theme::Text::Caption, true));
    for (int c = 1; c < headers.size(); ++c) {
        int w = head.horizontalAdvance(headers[c]);
        for (int i = 0; i < table->rowCount(); ++i)
            if (const QTableWidgetItem *it = table->item(i, c))
                w = std::max(w, body.horizontalAdvance(it->text()));
        table->setColumnWidth(c, w + theme::space::lg + theme::space::sm);
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setFixedHeight(table->horizontalHeader()->sizeHint().height()
                          + table->verticalHeader()->defaultSectionSize()
                                * static_cast<int>(rows.size())
                          + 2);
}

/// The width the table needs to show every column at its content width, the
/// ingredient names included. Anything wider is whitespace in the name column.
int breakdown_natural_width(const QTableWidget *table)
{
    const QFontMetrics fm(theme::font(theme::Text::Body));
    int name_w = table->horizontalHeader()->sectionSizeHint(0);
    for (int r = 0; r < table->rowCount(); ++r)
        if (const QTableWidgetItem *it = table->item(r, 0))
            name_w = std::max(name_w, fm.horizontalAdvance(it->text()) + table->iconSize().width()
                                          + theme::space::sm);
    // Item padding either side, plus a little air before the first number.
    int w = name_w + theme::space::lg + theme::space::xl;
    for (int c = 1; c < table->columnCount(); ++c) w += table->columnWidth(c);
    return w + 2 * table->frameWidth();
}

}  // namespace

///
/// A reference photo drawn at whatever height the layout asks for, keeping its aspect
/// ratio. A QLabel pixmap is fixed at the size it was scaled to; this rescales.
///
class PhotoLabel : public QWidget {
public:
    PhotoLabel() { setObjectName("clear"); }

    void set_source(const QPixmap &pm)
    {
        source_ = pm;
        set_photo_height(height_);
        update();
    }
    bool has_photo() const { return !source_.isNull(); }

    int width_at(int h) const
    {
        const double aspect =
            has_photo() ? double(source_.width()) / std::max(1, source_.height()) : 0.75;
        return static_cast<int>(std::round(h * aspect));
    }
    void set_photo_height(int h)
    {
        height_ = h;
        setFixedSize(width_at(h), h);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!has_photo()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 6, 6);
        p.setClipPath(clip);
        p.drawPixmap(rect(), source_);
    }

private:
    QPixmap source_;
    int height_ = 120;
};

namespace {

///
/// The finished-bowl card's arrangement. Three layouts, widest first:
///   wide     bowl | table | photos   (photos large, table at its content width)
///   medium   bowl over photos | table
///   stacked  bowl over photos, table below
/// Re-decided on resize and whenever the table or the photos change, since both
/// change width with the brand, the model and the units.
///
class BowlCard : public QWidget {
public:
    enum class Mode { Unset, Wide, Medium, Stacked };
    static constexpr int kBigPhoto = 250, kSmallPhoto = 120;

    BowlCard(QWidget *bowl, int bowl_width, QWidget *photos, QWidget *table,
             std::function<int()> table_width, std::function<int(int)> photos_width,
             std::function<void(int)> set_photo_height)
        : bowl_width_(bowl_width), photos_(photos), table_(table),
          table_width_(std::move(table_width)), photos_width_(std::move(photos_width)),
          set_photo_height_(std::move(set_photo_height))
    {
        setObjectName("clear");
        left_ = new QWidget;
        left_->setObjectName("clear");
        left_->setFixedWidth(bowl_width_);
        left_layout_ = new QVBoxLayout(left_);
        left_layout_->setContentsMargins(0, 0, 0, 0);
        left_layout_->setSpacing(theme::space::md);
        left_layout_->addWidget(bowl);

        grid_ = new QGridLayout(this);
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setHorizontalSpacing(theme::space::xl);
        grid_->setVerticalSpacing(theme::space::lg);
    }

    void relayout()
    {
        const int gap = theme::space::xl;
        const int table_w = table_width_();
        const int big = photos_width_(kBigPhoto);
        Mode want = Mode::Stacked;
        if (big > 0 && width() >= bowl_width_ + gap + big + gap + table_w)
            want = Mode::Wide;
        else if (width() >= bowl_width_ + gap + table_w)
            want = Mode::Medium;
        // Wide pins the table to its content width, so the name column carries no
        // slack; the other layouts let it fill what is left. Reapplied even when
        // the mode holds, because the content width follows the model and units.
        table_->setMinimumWidth(want == Mode::Wide ? table_w : 0);
        table_->setMaximumWidth(want == Mode::Wide ? table_w : QWIDGETSIZE_MAX);
        set_photo_height_(want == Mode::Wide ? kBigPhoto : kSmallPhoto);
        if (want == mode_) return;
        mode_ = want;

        for (QWidget *w : {static_cast<QWidget *>(left_), photos_, table_}) grid_->removeWidget(w);
        left_layout_->removeWidget(photos_);
        for (int i = 0; i < 3; ++i) grid_->setColumnStretch(i, 0);

        switch (mode_) {
        case Mode::Wide:
            // The table is the diagram's legend, so the two sit together; the photos
            // are reference material and take the far edge. Spare width opens up
            // between the table and the photos, never between bowl and legend.
            grid_->addWidget(left_, 0, 0, Qt::AlignTop);
            grid_->addWidget(table_, 0, 1, Qt::AlignTop);
            grid_->addWidget(photos_, 0, 2, Qt::AlignTop | Qt::AlignRight);
            grid_->setColumnStretch(2, 1);
            break;
        case Mode::Medium:
            left_layout_->addWidget(photos_, 0, Qt::AlignHCenter);
            grid_->addWidget(left_, 0, 0, Qt::AlignTop);
            grid_->addWidget(table_, 0, 1, Qt::AlignTop);
            grid_->setColumnStretch(1, 1);
            break;
        case Mode::Stacked:
        case Mode::Unset:
            left_layout_->addWidget(photos_, 0, Qt::AlignHCenter);
            grid_->addWidget(left_, 0, 0, Qt::AlignTop | Qt::AlignHCenter);
            grid_->addWidget(table_, 1, 0);
            grid_->setColumnStretch(0, 1);
            break;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout();
    }

private:
    int bowl_width_;
    QWidget *left_ = nullptr, *photos_, *table_;
    QVBoxLayout *left_layout_ = nullptr;
    QGridLayout *grid_ = nullptr;
    std::function<int()> table_width_;
    std::function<int(int)> photos_width_;
    std::function<void(int)> set_photo_height_;
    Mode mode_ = Mode::Unset;
};

}  // namespace

//
// ############################################################################
//

MainWindow::MainWindow(const QString &asset_dir, QWidget *parent)
    : QMainWindow(parent), asset_dir_(asset_dir)
{
    QString map_err;
    if (!load_name_mappings(mappings_path(), name_maps_, &map_err))
        load_warnings_ << map_err;
    if (!load_piece_heights(pieces_path(), pieces_, &map_err)) load_warnings_ << map_err;
    if (!load_mound_slopes(mounds_path(), mound_slopes_, &map_err)) load_warnings_ << map_err;
    LoadResult loaded = load_menus(asset_dir_ + "/menus", &name_maps_, &pieces_);
    menus_ = std::move(loaded.menus);
    load_warnings_ << loaded.warnings;
    menu_issues_ = loaded.issues;

    QString cost_err;
    if (!costs_.load_csv(asset_dir_ + "/data/ingredient_costs.csv", &cost_err))
        load_warnings_ << QString("ingredient_costs.csv: %1").arg(cost_err);

    // Restore the display preferences before the first paint, so nothing is built
    // in one unit and relabelled in the other.
    QSettings prefs;
    if (prefs.contains("units/metric") && !units::chosen_on_cli())
        units::set(prefs.value("units/metric").toBool() ? units::Volume::Millilitres
                                                        : units::Volume::FluidOunces);
    if (prefs.contains("theme/dark")) theme::set_dark(prefs.value("theme/dark").toBool());

    build_ui();
    if (units::metric()) {
        const QSignalBlocker block(capacity_);
        capacity_->setDecimals(0);
        capacity_->setSingleStep(25);
        capacity_->setValue(units::from_oz(32));
        if (capacity_label_) capacity_label_->setText("Functional fill (ml)");
    }
    reload_theme();
    // After build_ui: the status line it updates lives there.
    load_warnings_ << reload_measurements();
    populate_brand();

    // Stale menu data is shown in the header, where it stays visible without
    // blocking every launch. Only a file that could not be read stops the user.
    if (!load_warnings_.isEmpty())
        QMessageBox::warning(this, "Some files could not be loaded",
                             load_warnings_.join("\n"));
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
    resize(1440, 940);

    auto *central = new QWidget;
    central->setObjectName("clear");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(space::xl, space::lg, space::xl, space::lg);
    root->setSpacing(space::lg);
    root->addWidget(build_header());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(build_controls());
    splitter->addWidget(build_results());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({400, 1040});
    root->addWidget(splitter, 1);

    setCentralWidget(central);
}

QWidget *MainWindow::build_header()
{
    auto *header = new QWidget;
    header->setObjectName("clear");
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(space::md);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto *title = new QLabel("Bowl Fill Simulator");
    title->setObjectName("title");
    titles->addWidget(title);
    titles->addWidget(make_label("Lab37 · assembly line portioning", "subtitle"));
    h->addLayout(titles);
    h->addStretch(1);

    // The dispense model changes the whole results column, so it lives up here
    // rather than among the settings it switches between.
    mode_legacy_ = make_toggle("Legacy ramp");
    mode_adaptive_ = make_toggle("Adaptive");
    mode_legacy_->setChecked(true);
    mode_legacy_->setToolTip("What dynamic_portion_algorithm.cc does today: flat gram "
                             "steps added until a mass floor is cleared.");
    mode_adaptive_->setToolTip("What the user stories propose: solve directly for a "
                               "volume target, holding each ingredient inside its "
                               "tolerance band.");
    connect(mode_legacy_, &QPushButton::clicked, this, [this] { set_mode(false); });
    connect(mode_adaptive_, &QPushButton::clicked, this, [this] { set_mode(true); });
    auto *modes = segment({mode_legacy_, mode_adaptive_});
    modes->setMinimumWidth(240);
    h->addWidget(modes);
    h->addSpacing(space::sm);

    unit_oz_ = make_toggle("fl oz");
    unit_ml_ = make_toggle("ml");
    unit_oz_->setToolTip("Show volumes in US fluid ounces");
    unit_ml_->setToolTip("Show volumes in millilitres (1 fl oz = 29.5735 ml)");
    unit_oz_->setChecked(!units::metric());
    unit_ml_->setChecked(units::metric());
    connect(unit_oz_, &QPushButton::clicked, this, [this] { set_units(false); });
    connect(unit_ml_, &QPushButton::clicked, this, [this] { set_units(true); });
    h->addWidget(segment({unit_oz_, unit_ml_}));
    h->addSpacing(space::sm);

    issues_ = new QPushButton;
    issues_->setObjectName("issues");
    issues_->setCursor(Qt::PointingHandCursor);
    issues_->setVisible(!menu_issues_.isEmpty());
    issues_->setText(QString("%1 menu issue%2")
                         .arg(menu_issues_.size())
                         .arg(menu_issues_.size() == 1 ? "" : "s"));
    issues_->setToolTip(menu_issues_.join("\n"));
    connect(issues_, &QPushButton::clicked, this, &MainWindow::show_issues);
    h->addWidget(issues_);

    about_ = new QPushButton("About");
    about_->setObjectName("quiet");
    about_->setToolTip(QString(kIntroText).arg(units::suffix()));
    connect(about_, &QPushButton::clicked, this, [this] {
        QMessageBox box(this);
        box.setWindowTitle("About the simulator");
        box.setTextFormat(Qt::RichText);
        box.setText(QString(kIntroText).arg(units::suffix()));
        box.exec();
    });
    h->addWidget(about_);

    theme_ = new QPushButton("Theme");
    theme_->setObjectName("quiet");
    theme_->setToolTip("Switch between the light and dark palettes");
    connect(theme_, &QPushButton::clicked, this, &MainWindow::toggle_theme);
    h->addWidget(theme_);
    return header;
}

void MainWindow::show_issues()
{
    QMessageBox box(this);
    box.setWindowTitle("Menu issues");
    box.setIcon(QMessageBox::Information);
    box.setText("These menus loaded, but some of their data looks stale. The items "
                "listed were left out of the simulation.");
    box.setInformativeText("Fix them in the menu export; the notice clears once the "
                           "export stops producing them.");
    box.setDetailedText(menu_issues_.join("\n"));
    box.exec();
}

QWidget *MainWindow::build_controls()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(380);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *host = new QWidget;
    host->setObjectName("clear");
    auto *col = new QVBoxLayout(host);
    // A panel border sitting exactly on the scroll area's edge can lose its outer
    // pixel column to rounding at fractional display scales, which shows on the
    // coloured verdict border. A small inset keeps every border inside the viewport.
    col->setContentsMargins(kEdgeInset, kEdgeInset, space::md, kEdgeInset);
    col->setSpacing(space::md);

    // ---- the bowl: what is being ordered ---------------------------------
    QVBoxLayout *v = nullptr;
    col->addWidget(titled_panel("Bowl", v));

    brand_ = make_combo();
    connect(brand_, &QComboBox::currentIndexChanged, this, &MainWindow::on_brand_changed);
    v->addWidget(field("Brand", brand_));

    recipe_ = make_combo();
    connect(recipe_, &QComboBox::currentIndexChanged, this, &MainWindow::on_recipe_changed);
    auto *recipe_row = new QHBoxLayout;
    recipe_row->setSpacing(space::sm);
    recipe_row->addWidget(field("Recipe", recipe_), 1);
    auto *report = new QPushButton("Report");
    report->setToolTip("Feasibility report: every recipe this brand defines, checked "
                       "against the current bowl and curves under both dispense models.");
    connect(report, &QPushButton::clicked, this, &MainWindow::show_report);
    recipe_row->addWidget(report, 0, Qt::AlignBottom);
    v->addLayout(recipe_row);
    recipe_note_ = make_label("", "note");
    v->addWidget(recipe_note_);

    for (int i = 0; i < 2; ++i) {
        QComboBox *&box = (i == 0 ? base1_ : base2_);
        box = make_combo();
        connect(box, &QComboBox::currentIndexChanged, this,
                &MainWindow::on_selection_changed);
    }
    v->addLayout(field_row({field("Base (bottom layer)", base1_), field("Second base", base2_)}));

    v->addSpacing(space::xs);
    v->addWidget(section_label("Proteins · up to 2"));
    protein_picks_ = new QWidget;
    protein_picks_->setObjectName("clear");
    new QGridLayout(protein_picks_);
    protein_picks_->layout()->setContentsMargins(0, 0, 0, 0);
    v->addWidget(protein_picks_);

    v->addSpacing(space::xs);
    v->addWidget(section_label("Toppings"));
    topping_picks_ = new QWidget;
    topping_picks_->setObjectName("clear");
    new QGridLayout(topping_picks_);
    topping_picks_->layout()->setContentsMargins(0, 0, 0, 0);
    v->addWidget(topping_picks_);

    // Sauces go in cups, never into the bowl: each one picked is a cup that takes
    // room from the food.
    v->addSpacing(space::xs);
    v->addWidget(section_label(QString("Sauces · up to %1 · one cup each")
                                   .arg(SauceCups::kMax)));
    sauce_picks_ = new QWidget;
    sauce_picks_->setObjectName("clear");
    new QGridLayout(sauce_picks_);
    sauce_picks_->layout()->setContentsMargins(0, 0, 0, 0);
    v->addWidget(sauce_picks_);

    // ---- dispense: how the model is set up --------------------------------
    col->addWidget(titled_panel("Dispense", v));

    method_robot_ = make_toggle("Robot");
    method_hand_ = make_toggle("Hand");
    method_pooled_ = make_toggle("Pooled");
    method_robot_->setChecked(true);
    for (QPushButton *b : {method_robot_, method_hand_, method_pooled_}) {
        connect(b, &QPushButton::clicked, this, [this, b] {
            for (QPushButton *o : {method_robot_, method_hand_, method_pooled_})
                o->setChecked(o == b);
            on_selection_changed();
        });
    }
    v->addWidget(field("Measurement source", segment({method_robot_, method_hand_,
                                                      method_pooled_})));
    method_note_ = make_label("", "note");
    v->addWidget(method_note_);

    capacity_ = make_spin(1, 6000, 1, 0);
    capacity_->setValue(32);
    capacity_->setToolTip("How full a bowl is meant to be: 32 oz, a fill line a little "
                          "below the rim of the real bowl (1058 ml to the rim). Food is "
                          "judged against this line; the rim is the hard limit.");
    connect(capacity_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    auto *cap_field = field("Functional fill (fl oz)", capacity_);
    capacity_label_ = cap_field->findChild<QLabel *>();
    v->addLayout(field_row({cap_field}));

    // Fit by height: cups and chunks need clearance below the lid, which a plain
    // volume sum cannot see. The dimensions live in Advanced; the switch and the cup
    // placement, which change every result, live here.
    QSettings prefs;
    heights_ = new QCheckBox("Judge fit by height under the lid");
    heights_->setChecked(prefs.value("geometry/enabled", true).toBool());
    heights_->setToolTip(QString("Rigid sauce cups and chunky pieces need room below the "
                                 "lid, not just their volume. Off: each cup counts as the "
                                 "%1 ml it takes up.").arg(SauceCups{}.cup_ml, 0, 'f', 0));
    v->addWidget(heights_);
    cups_pressed_ = make_toggle("Cups pressed in");
    cups_resting_ = make_toggle("Cups resting on top");
    cups_pressed_->setToolTip("The robot pushes each cup down; food flows round it, so a "
                              "cup costs only its own column.");
    cups_resting_->setToolTip("Cups sit on the food as dispensed, so the whole surface must "
                              "stay a cup height below the lid.");
    const bool pressed = prefs.value("geometry/cups_pressed", true).toBool();
    cups_pressed_->setChecked(pressed);
    cups_resting_->setChecked(!pressed);
    cups_segment_ = segment({cups_pressed_, cups_resting_});
    v->addWidget(cups_segment_);
    mound_ = new QCheckBox("Show mounding (food piles at the centre)");
    mound_->setChecked(prefs.value("mound/enabled", false).toBool());
    mound_->setToolTip("Food dropped at the centre piles up instead of levelling out, so a "
                       "bowl can stand over the rim while there is room to spare by "
                       "volume. Changes the diagram and reports the peak; fit is still "
                       "judged on volume, since the lidder presses the mound down. The "
                       "slope is under Advanced → Bowl, lid and cups.");
    connect(mound_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("mound/enabled", on);
        recompute();
    });
    v->addWidget(mound_);
    geometry_note_ = make_label("", "note");
    v->addWidget(geometry_note_);
    connect(heights_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("geometry/enabled", on);
        cups_segment_->setVisible(on);
        recompute();
    });
    for (QPushButton *b : {cups_pressed_, cups_resting_})
        connect(b, &QPushButton::clicked, this, [this, b] {
            cups_pressed_->setChecked(b == cups_pressed_);
            cups_resting_->setChecked(b == cups_resting_);
            QSettings().setValue("geometry/cups_pressed", cups_pressed_->isChecked());
            recompute();
        });
    cups_segment_->setVisible(heights_->isChecked());

    legacy_group_ = new QWidget;
    legacy_group_->setObjectName("clear");
    auto *lg = new QVBoxLayout(legacy_group_);
    lg->setContentsMargins(0, 0, 0, 0);
    lg->setSpacing(space::sm);
    floor_ = make_spin(0, 5000, 5, 0);
    floor_->setToolTip("minimum_product_weight_g, matched from the menu. A value you type "
                       "stays until you change brand or use the menu's value again.");
    // Programmatic updates are signal-blocked, so this only fires for typed values.
    connect(floor_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        floor_override_ = v;
        apply_floor();
        recompute();
    });
    lg->addWidget(field("Weight floor (g)", floor_));
    floor_note_ = make_label("", "note");
    lg->addWidget(floor_note_);
    floor_reset_ = new QPushButton("Use the menu's floor");
    floor_reset_->setObjectName("quiet");
    floor_reset_->setVisible(false);
    connect(floor_reset_, &QPushButton::clicked, this, [this] {
        floor_override_.reset();
        apply_floor();
        recompute();
    });
    lg->addWidget(floor_reset_, 0, Qt::AlignLeft);
    v->addWidget(legacy_group_);

    adaptive_group_ = new QWidget;
    adaptive_group_->setObjectName("clear");
    auto *ag = new QVBoxLayout(adaptive_group_);
    ag->setContentsMargins(0, 0, 0, 0);
    ag->setSpacing(space::md);
    target_fill_ = make_spin(10, 100, 1, 0);
    target_fill_->setValue(85);
    band_low_ = make_spin(0, 100, 1, 0);
    band_low_->setValue(75);
    ag->addLayout(field_row({field("Target fill %", target_fill_),
                             field("Leave alone above %", band_low_)}));
    tol_base_ = make_spin(0, 100, 1, 0);      tol_base_->setValue(20);
    tol_protein_ = make_spin(0, 100, 1, 0);   tol_protein_->setValue(5);
    tol_topping_ = make_spin(0, 100, 1, 0);   tol_topping_->setValue(10);
    ag->addLayout(field_row({field("Base ±%", tol_base_), field("Protein ±%", tol_protein_),
                             field("Topping ±%", tol_topping_)}));
    menu_price_ = make_spin(0, 100, 0.25, 2);
    menu_price_->setValue(12.95);
    menu_price_->setPrefix("$");
    cogs_target_ = make_spin(0, 100, 1, 0);
    cogs_target_->setValue(23);
    ag->addLayout(field_row({field("Menu price", menu_price_),
                             field("COGS target %", cogs_target_)}));
    adaptive_note_ = make_label("", "note");
    ag->addWidget(adaptive_note_);
    for (QDoubleSpinBox *sp : {target_fill_, band_low_, tol_base_, tol_protein_,
                               tol_topping_, menu_price_, cogs_target_})
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    v->addWidget(adaptive_group_);
    adaptive_group_->setVisible(false);

    // ---- advanced: per-ingredient tuning, collapsed by default ------------
    QFrame *adv = make_panel();
    auto *av = new QVBoxLayout(adv);
    av->setContentsMargins(space::lg, space::md, space::lg, space::md);
    av->setSpacing(space::md);
    auto *disclose = new QToolButton;
    disclose->setObjectName("disclosure");
    disclose->setText("ADVANCED: RAMP, COMPRESSION, DATA");
    disclose->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    disclose->setCheckable(true);
    disclose->setCursor(Qt::PointingHandCursor);
    disclose->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    av->addWidget(disclose);

    auto *adv_body = new QWidget;
    adv_body->setObjectName("clear");
    v = new QVBoxLayout(adv_body);
    v->setContentsMargins(0, space::xs, 0, space::xs);
    v->setSpacing(space::md);
    av->addWidget(adv_body);

    const bool open = QSettings().value("ui/advanced_open", false).toBool();
    disclose->setChecked(open);
    disclose->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    adv_body->setVisible(open);
    connect(disclose, &QToolButton::toggled, this, [disclose, adv_body](bool on) {
        disclose->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        adv_body->setVisible(on);
        QSettings().setValue("ui/advanced_open", on);
    });

    auto *edits_row = new QHBoxLayout;
    edits_row->addWidget(section_label("Base ramp"), 1);
    auto *reset_edits = new QPushButton("Reset edits to menu values");
    reset_edits->setObjectName("quiet");
    reset_edits->setToolTip("Your edits to ramp values and the weight floor are kept as you "
                            "change the bowl, until you change brand. This drops them now.");
    connect(reset_edits, &QPushButton::clicked, this, [this] {
        overrides_.clear();
        floor_override_.reset();
        on_selection_changed();
    });
    edits_row->addWidget(reset_edits);
    v->addLayout(edits_row);
    for (int i = 0; i < 2; ++i) {
        auto *card = make_panel("sunk");
        auto *g = new QGridLayout(card);
        g->setContentsMargins(space::md, space::md, space::md, space::md);
        g->setHorizontalSpacing(space::sm);
        g->setVerticalSpacing(space::sm);
        base_start_[i] = make_spin(0, 2000, 1, 0);
        base_step_[i] = make_spin(0, 100, 0.1, 2);
        base_max_[i] = make_spin(0, 2000, 1, 0);
        g->addWidget(make_label(i == 0 ? "Base" : "Second base", "label"), 0, 0, 1, 3);
        g->addWidget(field("Start g", base_start_[i]), 1, 0);
        g->addWidget(field("Step g", base_step_[i]), 1, 1);
        g->addWidget(field("Max g", base_max_[i]), 1, 2);
        base_fit_[i] = make_label("", "note");
        g->addWidget(base_fit_[i], 2, 0, 1, 3);
        // An edit belongs to the base in this slot, so it follows that ingredient: switch
        // bases and back and the edit is still there. Programmatic updates are
        // signal-blocked, so only typed values land here.
        const std::pair<QDoubleSpinBox *, std::optional<double> Override::*> fields[] = {
            {base_start_[i], &Override::start_g}, {base_step_[i], &Override::step_g},
            {base_max_[i], &Override::max_g}};
        for (const auto &[spin, member] : fields)
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, i, member](double v) {
                const QString name = (i == 0 ? base1_ : base2_)->currentData().toString();
                if (!name.isEmpty()) overrides_[name].*member = v;
                recompute();
            });
        base_card_[i] = card;
        base_card_layout_[i] = g;
        v->addWidget(card);
    }
    split_ = new QCheckBox("Apply the 50:50 base split");
    split_->setChecked(true);
    connect(split_, &QCheckBox::toggled, this, &MainWindow::on_selection_changed);
    v->addWidget(split_);
    split_note_ = make_label("", "note");
    v->addWidget(split_note_);

    v->addWidget(hline());
    v->addWidget(section_label("Protein and topping ramp"));
    // Each ingredient takes two lines, its name over its four values, so names are
    // never cut short in a sidebar this narrow.
    override_table_ = new QWidget;
    override_table_->setObjectName("clear");
    auto *og = new QGridLayout(override_table_);
    og->setContentsMargins(0, 0, 0, 0);
    og->setHorizontalSpacing(space::sm);
    og->setVerticalSpacing(space::xs);
    v->addWidget(override_table_);

    v->addWidget(hline());
    v->addWidget(section_label("Bowl, lid and cups"));
    {
        QSettings prefs;
        // Keys are full paths; the cup's live under cups/, so the measured cup replaced
        // whatever placeholder an earlier version had saved.
        auto dim = [&](const QString &key, double def, double hi, int dp, const char *suffix) {
            QDoubleSpinBox *sp = make_spin(0, hi, dp ? 0.5 : 1, dp);
            sp->setSuffix(suffix);
            sp->setValue(prefs.value(key, def).toDouble());
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key](double v) {
                QSettings().setValue(key, v);
                recompute();
            });
            return sp;
        };
        // The bowl and lid live under bowl/ and lid/, so the measured values replaced
        // the placeholders an earlier version saved under geometry/.
        const BowlGeometry measured;
        depth_ = dim("bowl/depth_mm", measured.depth_mm, 200, 1, " mm");
        depth_->setToolTip("Inside depth, floor to rim (measured: 46 mm; 51 mm outside).");
        bowl_top_d_ = dim("bowl/top_diameter_mm", measured.top_diameter_mm, 400, 1, " mm");
        bowl_top_d_->setToolTip("Inside diameter at the rim (measured: 181 mm).");
        bowl_bottom_d_ = dim("bowl/bottom_diameter_mm", measured.bottom_diameter_mm, 400, 1,
                             " mm");
        bowl_bottom_d_->setToolTip("Inside diameter at the floor (measured: 161 mm).");
        headroom_ = dim("lid/headroom_mm", measured.lid_headroom_mm, 50, 1, " mm");
        headroom_->setToolTip("How far the inside of the lid rises above the rim in its "
                              "centre (measured: about 5 mm).");
        lid_plateau_d_ = dim("lid/plateau_diameter_mm", measured.lid_plateau_diameter_mm, 400,
                             1, " mm");
        lid_plateau_d_->setToolTip("Across the lid's raised centre (measured: about 160 mm).");
        const BowlGeometry &cup = measured;
        cup_h_ = dim("cups/height_mm", cup.cup_height_mm, 100, 1, " mm");
        cup_h_->setToolTip("Sauce cup height with its lid on (measured: 30 mm).");
        cup_d_ = dim("cups/top_diameter_mm", cup.cup_diameter_mm, 150, 1, " mm");
        cup_d_->setToolTip("Across the lid (measured: 65 mm). The cup tapers to its base, "
                           "so a pressed-in cup displaces less than a straight column.");
        cup_base_d_ = dim("cups/base_diameter_mm", cup.cup_base_diameter_mm, 150, 1, " mm");
        cup_base_d_->setToolTip("Across the base (measured: 45 mm).");
        chunk_proud_ = dim("geometry/chunk_proud_pct", BowlGeometry::kPlaceholderChunkProud * 100, 100,
                           0, " %");
        chunk_proud_->setToolTip("How much of a chunky piece's height stands above the "
                                 "smeared food around it.");
        v->addLayout(field_row({field("Bowl depth", depth_), field("Rim diameter", bowl_top_d_),
                                field("Floor diameter", bowl_bottom_d_)}));
        v->addLayout(field_row({field("Lid rises", headroom_),
                                field("Over a centre of", lid_plateau_d_)}));
        v->addLayout(field_row({field("Cup height", cup_h_), field("Lid diameter", cup_d_),
                                field("Base diameter", cup_base_d_)}));
        mound_slope_ = dim("mound/slope_deg", BowlGeometry::kPlaceholderMoundSlope, 80, 0,
                           "°");
        mound_slope_->setToolTip("With Show mounding on: how steep the pile's sides are, "
                                 "the food's angle of repose. 0° is a level fill. Read "
                                 "it off a side-on triage photo: the peak's rise over the "
                                 "height at the wall, across the distance between them.");
        v->addLayout(field_row({field("Chunks stand proud by", chunk_proud_),
                                field("Mound slope", mound_slope_)}));
        v->addWidget(make_label("Piece heights for chunky ingredients are set per ingredient "
                                "under <i>Ingredients</i> below.", "note"));
    }

    // How far the robot misses its target weights: one of the three inputs to the
    // finished bowl's range and overfill probabilities.
    v->addWidget(hline());
    v->addWidget(section_label("Dispense variation"));
    {
        QSettings prefs;
        const DispenseVariation def;
        auto pct = [&](const char *key, double fallback, double lo = 0) {
            QDoubleSpinBox *sp = make_spin(lo, 50, 0.5, 1);
            sp->setSuffix(" %");
            // Kept under dispense/, so the measured defaults replaced the placeholders
            // an earlier version saved under uncertainty/.
            sp->setValue(prefs.value(QString("dispense/") + key, fallback * 100).toDouble());
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key](double val) {
                QSettings().setValue(QString("dispense/") + key, val);
                recompute();
            });
            return sp;
        };
        disp_base_ = pct("base_pct", def.base);
        disp_protein_ = pct("protein_pct", def.protein);
        disp_topping_ = pct("topping_pct", def.topping);
        v->addLayout(field_row({field("Base ±", disp_base_), field("Protein ±", disp_protein_),
                                field("Topping ±", disp_topping_)}));
        bias_base_ = pct("base_bias_pct", def.base_bias, -50);
        bias_protein_ = pct("protein_bias_pct", def.protein_bias, -50);
        bias_topping_ = pct("topping_bias_pct", def.topping_bias, -50);
        for (QDoubleSpinBox *sp : {bias_base_, bias_protein_, bias_topping_})
            sp->setToolTip("How far over (or, negative, under) its target the robot "
                           "dispenses on average. Shifts the median bowl and its range; "
                           "the ramp and the adaptive solver still plan on the targets.");
        v->addLayout(field_row({field("Base bias", bias_base_),
                                field("Protein bias", bias_protein_),
                                field("Topping bias", bias_topping_)}));
        v->addWidget(make_label("How far the robot misses its target weight: one standard "
                                "deviation (±), and its average lean (bias), as shares of "
                                "the target. Defaults from 90 days "
                                "of production dispenses (to 2026-10-12), leaving out "
                                "spinach and frozen peas, whose targets look wrong.", "note"));
    }

    v->addWidget(hline());
    v->addWidget(section_label("Compression"));
    compress_ = new QCheckBox("Compress the base under the load above it");
    compress_->setChecked(true);
    connect(compress_, &QCheckBox::toggled, this, &MainWindow::on_selection_changed);
    v->addWidget(compress_);
    load_transfer_ = make_spin(0, kMaxLoadTransfer, 0.05, 2);
    load_transfer_->setValue(1.0);
    load_transfer_->setToolTip("Share of the topping weight that bears on the base. 1 is "
                               "the weight alone; above 1 is fitted from scanned bowls, "
                               "where lid, cups and the drop squash more than weight "
                               "explains.");
    connect(load_transfer_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    v->addWidget(field("Load transfer", load_transfer_));
    compress_note_ = make_label("", "note");
    v->addWidget(compress_note_);

    v->addWidget(hline());
    v->addWidget(section_label("Mass → volume data"));
    auto *row = new QHBoxLayout;
    row->setSpacing(space::sm);
    auto *upload = new QPushButton("Upload CSV…");
    upload->setToolTip("One row per measurement: ingredient, g, ml. The file is copied "
                       "into data/measurements and loaded from there from now on.");
    connect(upload, &QPushButton::clicked, this, &MainWindow::on_upload_csv);
    row->addWidget(upload, 1);
    auto *help = new QPushButton("Format");
    help->setToolTip(CurveSet::format_help());
    connect(help, &QPushButton::clicked, this, &MainWindow::show_csv_help);
    row->addWidget(help);
    v->addLayout(row);

    // Whole scanned bowls: single-ingredient rows extend the curves, mixed rows fit
    // how hard the lower layers are squashed.
    auto *bowl_row = new QHBoxLayout;
    bowl_row->setSpacing(space::sm);
    auto *bowls = new QPushButton("Upload bowl data…");
    bowls->setToolTip("One row per scanned bowl: grams per ingredient in dispense order, "
                      "then ml. Refits the curves and the base squash. The file is copied "
                      "into data/measurements and loaded from there from now on.");
    connect(bowls, &QPushButton::clicked, this, &MainWindow::on_upload_bowl_data);
    bowl_row->addWidget(bowls, 1);
    auto *bowl_help = new QPushButton("Format");
    bowl_help->setToolTip(bowl_data_format_help());
    connect(bowl_help, &QPushButton::clicked, this, &MainWindow::show_bowl_help);
    bowl_row->addWidget(bowl_help);
    v->addLayout(bowl_row);

    // Every measurement lives as a file in data/measurements; editing or deleting
    // those files and reloading is how data is corrected.
    auto *store_row = new QHBoxLayout;
    store_row->setSpacing(space::sm);
    auto *reload = new QPushButton("Reload");
    reload->setToolTip("Re-read every file in data/measurements, after editing or "
                       "removing data by hand.");
    connect(reload, &QPushButton::clicked, this, &MainWindow::on_reset_curves);
    store_row->addWidget(reload, 1);
    auto *open_dir = new QPushButton("Open folder");
    open_dir->setToolTip("Open data/measurements in the file manager.");
    connect(open_dir, &QPushButton::clicked, this, [this] {
        QDir().mkpath(measurements_dir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(measurements_dir()));
    });
    store_row->addWidget(open_dir, 1);
    v->addLayout(store_row);

    // The camera's zero: what an empty bowl reads. Every scanned volume is corrected
    // by it, so small portions are not under-counted by a fixed few ml.
    empty_bowl_ml_ = make_spin(-30, 30, 0.5, 1);
    empty_bowl_ml_->setSuffix(" ml");
    empty_bowl_ml_->setValue(
        QSettings().value("measurements/empty_bowl_ml", kDefaultEmptyBowlMl).toDouble());
    empty_bowl_ml_->setToolTip("What volume-vision reads for an empty bowl (usually -4 to "
                               "-8 ml). Subtracted from every scanned bowl in "
                               "data/measurements before curves are fitted. Files with "
                               "ingredient, g, ml rows are taken as given.");
    connect(empty_bowl_ml_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QSettings().setValue("measurements/empty_bowl_ml", v);
        const QStringList problems = reload_measurements();
        if (!problems.isEmpty())
            QMessageBox::warning(this, "Some measurement files could not be loaded",
                                 problems.join("\n"));
        recompute();
    });
    v->addWidget(field("Empty-bowl reading (camera zero)", empty_bowl_ml_));

    // Both only change how the stored readings are read; the files are never edited,
    // so turning one off brings every reading back.
    auto measurement_toggle = [this](const QString &label, const char *key,
                                     const QString &tip) {
        auto *cb = new QCheckBox(label);
        cb->setChecked(QSettings().value(key, false).toBool());
        cb->setToolTip(tip);
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
            QSettings().setValue(key, on);
            const QStringList problems = reload_measurements();
            if (!problems.isEmpty())
                QMessageBox::warning(this, "Some measurement files could not be loaded",
                                     problems.join("\n"));
            recompute();
        });
        return cb;
    };
    backfill_ = measurement_toggle(
        "Backfill readings from the test-beans calibration", "measurements/backfill",
        "Rigid test beans cannot compress, so their true volume is proportional to their "
        "mass; any bend in their readings is the camera's. Inverting that bend corrects "
        "every other camera scan (most at small readings, none at the deepest). Needs a "
        "\"test ...\" ladder of at least 5 readings in data/measurements.");
    v->addWidget(backfill_);
    drop_impossible_ = measurement_toggle(
        "Ignore impossible readings", "measurements/drop_impossible",
        "Leave out of the fits any scan reading less than the food's own solid volume "
        "(mass / 1.05 g/ml): a pile cannot take up less room than it is made of. The "
        "test beans are exempt. The files are not changed.");
    v->addWidget(drop_impossible_);
    curve_note_ = make_label("", "note");
    v->addWidget(curve_note_);

    v->addWidget(hline());
    v->addWidget(section_label("Menu"));
    brand_note_ = make_label("", "note");
    v->addWidget(brand_note_);

    v->addWidget(hline());
    v->addWidget(section_label("Ingredients"));
    auto *names = new QPushButton("Edit names, kinds and piece heights…");
    names->setToolTip("Map each menu's spelling of an ingredient onto a common name, so "
                      "brands that call it different things share its cost and curve; "
                      "correct its kind; and give chunky ingredients a piece height.");
    connect(names, &QPushButton::clicked, this, &MainWindow::show_mappings);
    v->addWidget(names);
    mapping_note_ = make_label("", "note");
    v->addWidget(mapping_note_);
    update_mapping_note();

    col->addWidget(adv);
    col->addStretch(1);
    scroll->setWidget(host);
    return scroll;
}

QWidget *MainWindow::build_results()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *host = new QWidget;
    host->setObjectName("clear");
    auto *v = new QVBoxLayout(host);
    v->setContentsMargins(space::xs, kEdgeInset, space::md, kEdgeInset);
    v->setSpacing(space::md);

    // ---- verdict ----------------------------------------------------------
    verdict_panel_ = make_panel();
    auto *vv = new QVBoxLayout(verdict_panel_);
    vv->setContentsMargins(space::lg, space::md, space::lg, space::md);
    vv->setSpacing(space::xs);
    verdict_head_ = new QLabel;
    verdict_head_->setObjectName("heading");
    verdict_head_->setWordWrap(true);
    verdict_sub_ = make_label("", "lead");
    vv->addWidget(verdict_head_);
    vv->addWidget(verdict_sub_);
    v->addWidget(verdict_panel_);

    // ---- key numbers, one panel divided into columns ----------------------
    auto *stats = make_panel();
    auto *sh = new QHBoxLayout(stats);
    sh->setContentsMargins(0, space::md, 0, space::md);
    sh->setSpacing(0);
    for (int i = 0; i < 5; ++i) {
        auto *tile = new QFrame;
        tile->setObjectName(i == 0 ? "statFirst" : "stat");
        auto *tv = new QVBoxLayout(tile);
        tv->setContentsMargins(space::lg, 0, space::lg, 0);
        tv->setSpacing(space::xs);
        stat_key_[i] = make_label("", "statKey");
        stat_key_[i]->setWordWrap(false);
        stat_value_[i] = new QLabel("—");
        stat_value_[i]->setObjectName("statValue");
        tv->addWidget(stat_key_[i]);
        tv->addWidget(stat_value_[i]);
        stat_tile_[i] = tile;
        sh->addWidget(tile, 1);
    }
    v->addWidget(stats);

    // ---- the finished bowl and where its volume goes -----------------------
    // One card: the bowl and its reference photos on the left, and on the right the
    // breakdown table, which is also the bowl's legend.
    QVBoxLayout *bv = nullptr;
    v->addWidget(titled_panel("The finished bowl", bv));
    // The bowl as a distribution: what the median bowl does, the range 80% of real
    // bowls land in, and the odds of going over.
    fill_note_ = make_label("", "lead");
    bv->addWidget(fill_note_);

    constexpr int kBowlColumn = 380;   // the bowl, plus its level labels either side
    diagram_ = new BowlDiagram;
    diagram_->setFixedWidth(kBowlColumn);
    diagram_->setFixedHeight(diagram_->heightForWidth(kBowlColumn));

    auto *photos = new QWidget;
    photos->setObjectName("clear");
    auto *ph = new QHBoxLayout(photos);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->setSpacing(space::md);
    for (int i = 0; i < 2; ++i) {
        auto *pc = new QVBoxLayout;
        pc->setSpacing(space::xs);
        photo_[i] = new PhotoLabel;
        photo_caption_[i] = make_label("", "note");
        photo_caption_[i]->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        pc->addWidget(photo_[i], 0, Qt::AlignHCenter);
        pc->addWidget(photo_caption_[i]);
        pc->addStretch(1);
        ph->addLayout(pc);
    }

    auto *right = new QWidget;
    right->setObjectName("clear");
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(space::md);
    breakdown_ = new QTableWidget(0, 8);
    breakdown_->setObjectName("breakdown");
    breakdown_->setShowGrid(false);
    breakdown_->verticalHeader()->setVisible(false);
    breakdown_->verticalHeader()->setDefaultSectionSize(36);
    breakdown_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    breakdown_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    breakdown_->setSelectionMode(QAbstractItemView::NoSelection);
    breakdown_->setFocusPolicy(Qt::NoFocus);
    breakdown_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    breakdown_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rv->addWidget(breakdown_);
    foot_note_ = make_label("", "note");
    rv->addWidget(foot_note_);

    bowl_row_ = new BowlCard(
        diagram_, kBowlColumn, photos, right,
        [this] { return breakdown_natural_width(breakdown_); },
        [this](int h) {
            int w = 0;
            for (PhotoLabel *p : photo_)
                if (p->has_photo()) w += (w ? space::md : 0) + p->width_at(h);
            return w;
        },
        [this](int h) {
            for (int i = 0; i < 2; ++i) {
                photo_[i]->set_photo_height(h);
                photo_caption_[i]->setFixedWidth(
                    photo_[i]->has_photo() ? std::max(photo_[i]->width(), 140) : 220);
            }
        });
    bv->addWidget(bowl_row_);

    // ---- charts: one per model, only the active one shows -----------------
    QVBoxLayout *cv = nullptr;
    ramp_panel_ = titled_panel("Volume against mass, one point per ramp pass", cv);
    ramp_chart_ = new RampChart;
    cv->addWidget(ramp_chart_);
    v->addWidget(ramp_panel_);

    QVBoxLayout *tv = nullptr;
    tol_panel_ = titled_panel("How far each ingredient moved inside its tolerance", tv);
    tol_chart_ = new ToleranceChart;
    tv->addWidget(tol_chart_);
    tol_panel_->setVisible(false);
    v->addWidget(tol_panel_);

    QVBoxLayout *qv = nullptr;
    v->addWidget(titled_panel("Measured mass → volume curves", qv));
    curve_chart_ = new CurveChart;
    qv->addWidget(curve_chart_);

    v->addStretch(1);
    scroll->setWidget(host);
    return scroll;
}

void MainWindow::reload_theme()
{
    qApp->setStyleSheet(theme::stylesheet());
    theme_->setText(theme::is_dark() ? "Light" : "Dark");
    update();
}

void MainWindow::toggle_theme()
{
    theme::set_dark(!theme::is_dark());
    QSettings().setValue("theme/dark", theme::is_dark());
    reload_theme();
    recompute();
}

namespace {
const char *const kLegacyKeys[5] = {"Ramp stops at", "Final volume", "Fill",
                                    "Squeezed out", "Highest safe floor"};
const char *const kAdaptiveKeys[5] = {"Total mass", "Food volume", "Food fill",
                                      "With sauce cups", "COGS"};
}  // namespace

void MainWindow::set_mode(bool adaptive)
{
    adaptive_ = adaptive;
    mode_legacy_->setChecked(!adaptive);
    mode_adaptive_->setChecked(adaptive);
    legacy_group_->setVisible(!adaptive);
    adaptive_group_->setVisible(adaptive);
    for (int i = 0; i < 5; ++i)
        stat_key_[i]->setText(adaptive ? kAdaptiveKeys[i] : kLegacyKeys[i]);
    ramp_panel_->setVisible(!adaptive);
    tol_panel_->setVisible(adaptive);
    // The 50:50 split and the compression toggle both describe the legacy path.
    split_->setVisible(!adaptive);
    split_note_->setVisible(!adaptive);
    recompute();
}

void MainWindow::set_units(bool metric)
{
    const auto want = metric ? units::Volume::Millilitres : units::Volume::FluidOunces;
    if (want == units::current()) return;

    // The capacity box and the per-ingredient rates hold values in the displayed
    // unit, so convert what is already in them rather than reinterpreting the number.
    const double cap_oz = units::to_oz(capacity_->value());
    units::set(want);
    unit_oz_->setChecked(!metric);
    unit_ml_->setChecked(metric);
    QSettings().setValue("units/metric", metric);

    {
        const QSignalBlocker block(capacity_);
        capacity_->setDecimals(metric ? 0 : 1);
        capacity_->setSingleStep(metric ? 25 : 1);
        capacity_->setValue(units::from_oz(cap_oz));
    }
    if (capacity_label_)
        capacity_label_->setText(QString("Functional fill (%1)")
                                     .arg(metric ? "ml" : "fl oz"));
    about_->setToolTip(QString(kIntroText).arg(units::suffix()));
    rebuild_override_rows();   // the rate spin boxes are re-seeded in the new unit
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
    if (!method_hand_) return Method::Robot;
    if (method_hand_->isChecked()) return Method::Hand;
    if (method_pooled_->isChecked()) return Method::Pooled;
    return Method::Robot;
}

double MainWindow::value_for(const QString &name, const QString &fieldName) const
{
    // Your edits win, then the recipe's pinned weight, then the menu.
    auto it = overrides_.find(name);
    if (it != overrides_.end()) {
        const Override &o = it->second;
        if (fieldName == "start" && o.start_g) return *o.start_g;
        if (fieldName == "step" && o.step_g) return *o.step_g;
        if (fieldName == "max" && o.max_g) return *o.max_g;
        if (fieldName == "rate" && o.rate) return *o.rate;
    }
    if (fieldName == "start")
        if (auto pin = pins_.find(name); pin != pins_.end()) return pin->second;
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
        it.mound_slope_deg = mound_slope_for(it.name);
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
            it.mound_slope_deg = mound_slope_for(it.name);
            if (!it.has_curve) {
                it.flat_oz_per_100g = value_for(name, "rate");
            } else if (auto ov = overrides_.find(name);
                       ov != overrides_.end() && ov->second.curve_scale) {
                it.K *= *ov->second.curve_scale;   // same shape, more or less volume
            }
            items.push_back(it);
        }
    }
    return items;
}

//
// ############################################################################
// Population
//

QString MainWindow::mappings_path() const
{
    return asset_dir_ + "/data/ingredient_aliases.csv";
}

QString MainWindow::pieces_path() const
{
    return asset_dir_ + "/data/ingredient_pieces.csv";
}

QString MainWindow::mounds_path() const
{
    return asset_dir_ + "/data/ingredient_mounds.csv";
}

double MainWindow::mound_slope_for(const QString &name) const
{
    const auto it = mound_slopes_.find(name);
    return it != mound_slopes_.end() ? it->second : -1.0;
}

void MainWindow::update_mapping_note()
{
    if (!mapping_note_) return;
    QSet<QString> brands;
    for (const auto &[key, m] : name_maps_) brands << key.first;
    mapping_note_->setText(
        name_maps_.empty()
            ? QString("Nothing mapped yet. Saved to <code>data/ingredient_aliases.csv</code>.")
            : QString("<b>%1</b> spelling%2 mapped across %3 brand%4, saved in "
                      "<code>data/ingredient_aliases.csv</code>.")
                  .arg(name_maps_.size()).arg(name_maps_.size() == 1 ? "" : "s")
                  .arg(brands.size()).arg(brands.size() == 1 ? "" : "s"));
}

void MainWindow::show_mappings()
{
    MappingDialog dialog(menus_, name_maps_, pieces_, mound_slopes_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const NameMappings next = dialog.mappings();
    const PieceHeights next_pieces = dialog.pieces();
    const MoundSlopes next_slopes = dialog.slopes();
    QString err;
    if (!save_name_mappings(mappings_path(), next, &err)
        || !save_piece_heights(pieces_path(), next_pieces, &err)
        || !save_mound_slopes(mounds_path(), next_slopes, &err)) {
        QMessageBox::warning(this, "Could not save ingredients", err);
        return;
    }
    name_maps_ = next;
    pieces_ = next_pieces;
    mound_slopes_ = next_slopes;
    reload_menus();
}

void MainWindow::reload_menus()
{
    // Names may have changed under the current selection, so hold on to the brand and
    // recipe (recipe names come from the menu's template ids, not its ingredients).
    const QString brand = brand_->currentData().toString();
    const QString recipe = recipe_->currentData().toString();

    LoadResult loaded = load_menus(asset_dir_ + "/menus", &name_maps_, &pieces_);
    menus_ = std::move(loaded.menus);
    menu_issues_ = loaded.issues;
    issues_->setVisible(!menu_issues_.isEmpty());
    issues_->setText(QString("%1 menu issue%2")
                         .arg(menu_issues_.size())
                         .arg(menu_issues_.size() == 1 ? "" : "s"));
    issues_->setToolTip(menu_issues_.join("\n"));
    update_mapping_note();

    populate_brand();
    select(brand, recipe);
}

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
    // A new brand is the one thing that drops your edits: its ingredients and ramp
    // settings are different.
    overrides_.clear();
    pins_.clear();
    floor_override_.reset();
    picked_proteins_.clear();
    picked_toppings_.clear();
    picked_sauces_.clear();

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
    // The recipe's own weights are replaced; your edits (overrides_) are kept.
    pins_.clear();
    picked_proteins_.clear();
    picked_toppings_.clear();
    picked_sauces_.clear();

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
        // Every bowl comes with one sauce; the second is an upcharge.
        if (!m->sauces.isEmpty()) picked_sauces_ << m->sauces.front();
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
            pins_[ri.name] = ri.grams;
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
        picked_sauces_ = m->sauces_in(*rec).mid(0, SauceCups::kMax);

        const int n = static_cast<int>(rec->items.size()) - m->sauces_in(*rec).size();
        QString note = QString("Pins <b>%1</b> ingredient%2")
                           .arg(n)
                           .arg(n == 1 ? "" : "s");
        if (!picked_sauces_.isEmpty())
            note += QString(" and <b>%1</b> sauce cup%2 (%3)")
                        .arg(picked_sauces_.size())
                        .arg(picked_sauces_.size() == 1 ? "" : "s")
                        .arg(picked_sauces_.join(", "));
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

    auto *grid = qobject_cast<QGridLayout *>(sauce_picks_->layout());
    while (QLayoutItem *item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m->sauces.isEmpty()) {
        grid->addWidget(make_label("This menu serves no sauces in cups.", "note"), 0, 0);
        return;
    }
    int row = 0, col = 0;
    for (const QString &n : m->sauces) {
        auto *cb = new QCheckBox(n);
        cb->setChecked(picked_sauces_.contains(n));
        connect(cb, &QCheckBox::toggled, this, [this, n](bool on) {
            if (on && !picked_sauces_.contains(n)) picked_sauces_ << n;
            if (!on) picked_sauces_.removeAll(n);
            on_selection_changed();
        });
        grid->addWidget(cb, row, col);
        if (++col == 2) { col = 0; ++row; }
    }
}

int MainWindow::sauce_cup_count() const
{
    return std::min(static_cast<int>(picked_sauces_.size()), SauceCups::kMax);
}

void MainWindow::rebuild_override_rows()
{
    const Menu *m = menu();
    if (!m) return;
    const QStringList rows = picked_proteins_ + picked_toppings_;

    auto *grid = qobject_cast<QGridLayout *>(override_table_->layout());
    while (QLayoutItem *item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    rate_fields_.clear();
    override_table_->setVisible(!rows.isEmpty());
    if (rows.isEmpty()) return;

    const QString captions[4] = {"Start g", "Step g", "Max g",
                                 QString("%1/100 g").arg(units::suffix())};
    for (int c = 0; c < 4; ++c) grid->addWidget(make_label(captions[c], "label"), 0, c);

    const theme::Palette &pal = theme::palette();
    int line = 1;
    for (const QString &name : rows) {
        const Ingredient *ing = m->find(name);
        auto *label = new QLabel(name);
        label->setObjectName("clear");
        const bool guessed =
            ing && (ing->source == WeightSource::Class || ing->source == WeightSource::None);
        if (guessed) label->setStyleSheet(QString("color:%1;").arg(pal.over.name()));
        label->setToolTip(
            ing ? QString("Portion source: %1\nCurve: %2")
                      .arg(to_string(ing->source),
                           describe(choose_curve(name, ing->kind, curves_, method())))
                : QString());
        grid->addWidget(label, line++, 0, 1, 4);

        const char *fields[4] = {"start", "step", "max", "rate"};
        for (int c = 0; c < 4; ++c) {
            const bool is_rate = (c == 3);
            auto *spin = make_spin(0, is_rate ? 6000 : 2000, c == 1 ? 0.1 : 1,
                                   c == 1 ? 2 : is_rate ? (units::metric() ? 1 : 2) : 0);
            if (!is_rate) spin->setValue(value_for(name, fields[c]));
            const QString f = fields[c];
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, name, f](double v) {
                Override &o = overrides_[name];
                if (f == "start") o.start_g = v;
                else if (f == "step") o.step_g = v;
                else if (f == "max") o.max_g = v;
                if (f == "rate") set_rate(name, units::to_oz(v));
                else if (f == "start") refresh_rate_field(name);   // the curve's rate moves
                recompute();
            });
            grid->addWidget(spin, line, c);
            if (is_rate) rate_fields_[name].spin = spin;
        }
        auto *reset = new QToolButton;
        reset->setText(QString::fromUtf8("\u21BA"));
        reset->setAutoRaise(true);
        connect(reset, &QToolButton::clicked, this, [this, name] {
            if (auto it = overrides_.find(name); it != overrides_.end()) {
                it->second.rate.reset();
                it->second.curve_scale.reset();
            }
            refresh_rate_field(name);
            recompute();
        });
        grid->addWidget(reset, line, 4);
        rate_fields_[name].reset = reset;
        refresh_rate_field(name);
        ++line;
        grid->setRowMinimumHeight(line++, space::xs);
    }
}

void MainWindow::refresh_rate_field(const QString &name)
{
    const auto field = rate_fields_.find(name);
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(name) : nullptr;
    if (field == rate_fields_.end() || !ing) return;

    const BowlItem item = make_item(*ing, curves_, method());
    const auto ov = overrides_.find(name);
    const Override edit = ov != overrides_.end() ? ov->second : Override{};
    const QString unit = QString("%1/100 g").arg(units::suffix());
    auto shown = [&](double oz) { return QString("%1 %2").arg(units::from_oz(oz), 0, 'f', 1)
                                             .arg(unit); };
    double rate = 0;
    bool edited = false;
    QString tip;
    if (item.has_curve) {
        // The average over the portion, which is what the portion costs the bowl.
        const double g = value_for(name, "start") > 0 ? value_for(name, "start") : 100.0;
        const double curve = item.volume_oz(g) / g * 100.0;
        const double scale = edit.curve_scale.value_or(1.0);
        rate = curve * scale;
        edited = edit.curve_scale.has_value();
        tip = QString("Volume per 100 g at %1 g, from the %2.\n").arg(g, 0, 'f', 0)
                  .arg(describe(choose_curve(name, ing->kind, curves_, method())));
        tip += edited ? QString("Your edit scales the curve by \u00D7%1 (it gives %2 alone). "
                                "\u21BA goes back to the curve.")
                            .arg(scale, 0, 'f', 2).arg(shown(curve))
                      : QString("Edit it to scale the whole curve up or down; its shape "
                                "(how it packs as the portion grows) is kept.");
    } else {
        rate = value_for(name, "rate");
        edited = edit.rate.has_value();
        tip = QString("Nothing measured to lean on, so a flat rate is used.");
        if (edited) tip += QString(" \u21BA goes back to the default.");
    }
    const QSignalBlocker block(field->second.spin);
    field->second.spin->setValue(units::from_oz(rate));
    field->second.spin->setToolTip(tip);
    field->second.spin->setStyleSheet(
        edited ? QString("color:%1;").arg(theme::palette().accent.name()) : QString());
    if (field->second.reset) {
        field->second.reset->setVisible(edited);
        field->second.reset->setToolTip(item.has_curve ? "Back to the measured curve"
                                                       : "Back to the default rate");
    }
}

void MainWindow::set_rate(const QString &name, double oz_per_100g)
{
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(name) : nullptr;
    if (!ing) return;
    Override &o = overrides_[name];
    const BowlItem item = make_item(*ing, curves_, method());
    if (item.has_curve) {
        const double g = value_for(name, "start") > 0 ? value_for(name, "start") : 100.0;
        const double curve = item.volume_oz(g) / g * 100.0;
        if (curve > 0) o.curve_scale = oz_per_100g / curve;
    } else {
        o.rate = oz_per_100g;
    }
    refresh_rate_field(name);
}

void MainWindow::apply_floor()
{
    const Menu *m = menu();
    if (!m) return;
    const FloorRule *f = m->floor_for(bowl_names());
    const double menu_floor = f ? f->minimum_product_weight_g : 0.0;

    const QSignalBlocker block(floor_);
    floor_->setValue(floor_override_.value_or(menu_floor));
    floor_reset_->setVisible(floor_override_.has_value());
    if (floor_override_) {
        floor_note_->setText(QString("<b>Your value, %1 g</b>, is kept as you change the "
                                     "bowl. The menu's rule gives %2 for this bowl.")
                                 .arg(*floor_override_, 0, 'f', 0)
                                 .arg(f ? QString("%1 g").arg(menu_floor, 0, 'f', 0)
                                        : QString("no floor")));
        return;
    }

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
    // A bowl holds at most two cups, so the rest lock once two sauces are picked.
    if (auto *grid = qobject_cast<QGridLayout *>(sauce_picks_->layout()))
        for (int i = 0; i < grid->count(); ++i)
            if (auto *cb = qobject_cast<QCheckBox *>(grid->itemAt(i)->widget())) {
                cb->setChecked(picked_sauces_.contains(cb->text()));
                cb->setEnabled(cb->isChecked() || picked_sauces_.size() < SauceCups::kMax);
            }

    const bool two_bases = !base2_->currentData().toString().isEmpty();
    for (int i = 0; i < 2; ++i) {
        const QString name = (i == 0 ? base1_ : base2_)->currentData().toString();
        base_card_[i]->setVisible(!name.isEmpty());
        if (name.isEmpty()) continue;
        const Ingredient *ing = m->find(name);
        if (!ing) continue;

        const QSignalBlocker b1(base_start_[i]), b2(base_step_[i]), b3(base_max_[i]);
        const auto ov = overrides_.find(name);
        const Override edit = ov != overrides_.end() ? ov->second : Override{};
        const auto pin = pins_.find(name);
        base_start_[i]->setValue(edit.start_g     ? *edit.start_g
                                 : pin != pins_.end() ? pin->second
                                                      : split_base_start(*ing, split_->isChecked(),
                                                                         two_bases));
        base_step_[i]->setValue(edit.step_g.value_or(ing->step_increment_g));
        base_max_[i]->setValue(edit.max_g.value_or(ing->max_dispense_weight_g));
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

SimSettings MainWindow::legacy_settings() const
{
    SimSettings s;
    s.floor_g = floor_->value();
    s.bowl_capacity_oz = units::to_oz(capacity_->value());
    s.sauce.cups = sauce_cup_count();
    s.geometry = geometry();
    s.compress = compress_->isChecked();
    s.load_transfer = load_transfer_->value();
    s.dispense = dispense();
    return s;
}

void MainWindow::update_fill_note(const SimResult &r)
{
    const FillUncertainty &u = r.fill;
    if (!u.valid) {
        fill_note_->setText("");
        return;
    }
    const theme::Palette &pal = theme::palette();
    // Volumes relative to the food room: positive is over.
    auto rel = [&](double oz) {
        const double d = oz - u.room_oz;
        return QString("%1%2").arg(d >= 0 ? "+" : "−").arg(units::volume(std::fabs(d), true));
    };
    const double over = u.median_oz - u.room_oz;
    const QString median = over >= 0
        ? QString("<b>Median bowl: %1 over the food room</b>").arg(units::volume(over, true))
        : QString("<b>Median bowl: %1 under the food room</b>").arg(units::volume(-over, true));
    auto pct = [](double p) {
        const double v = p * 100;
        return v < 1 && v > 0 ? QString("&lt;1%") : v > 99 && v < 100 ? QString("&gt;99%")
                                                                    : QString("%1%").arg(std::round(v));
    };
    const QColor tone = u.p_over_room >= 0.5 ? pal.over : u.p_over_room >= 0.2 ? pal.mass : pal.accent;
    fill_note_->setText(
        QString("%1 (80% of bowls: %2 to %3)<br>"
                "<span style='color:%4'><b>%5</b> chance of overfilling the food room</span> · "
                "<b>%6</b> of going over the rim")
            .arg(median, rel(u.p10_oz), rel(u.p90_oz), tone.name(), pct(u.p_over_room),
                 pct(u.p_over_rim))
        + mound_line(r));
    fill_note_->setToolTip(
        QString("Food room %1 (the bowl less cups and chunk clearance); rim %2.\n"
                "Spread: each ingredient's bowl-to-bowl scatter, the uncertainty of its curve, "
                "and the dispense variation in Advanced, added as independent.")
            .arg(units::volume(u.room_oz, true), units::volume(u.rim_oz, true)));
}

QString MainWindow::mound_line(const SimResult &r) const
{
    const BowlGeometry &g = r.settings.geometry;
    const FillUncertainty &u = r.fill;
    if (!g.mound || r.frames.empty() || u.median_oz <= 0) return {};
    const std::vector<double> &oz = r.last().per_item_oz;
    // The 80% range moves every layer together, so each is scaled by the same share.
    auto above_lid = [&](double total_oz) {
        const double scale = std::max(0.0, total_oz) / u.median_oz;
        return mound_peak_mm(g, mound_layers(r.items, oz, g, scale)) - g.lid_mm();
    };
    auto signed_mm = [](double d) {
        return QString("%1%2 mm").arg(d >= 0 ? "+" : "\u2212").arg(std::fabs(d), 0, 'f', 0);
    };
    bool own = false, defaulted = false;
    for (size_t i = 0; i < r.items.size() && i < oz.size(); ++i) {
        if (oz[i] <= 0.005) continue;
        (r.items[i].mound_slope_deg >= 0 ? own : defaulted) = true;
    }
    const QString how = own ? QString("Mounded at each ingredient's slope")
                            : QString("Mounded at %1\u00B0").arg(g.mound_slope_deg, 0, 'f', 0);
    const double mid = above_lid(u.median_oz);
    QString text = QString("<br>%1, the median bowl peaks <b>%2 mm %3 the lid</b>")
                       .arg(how).arg(std::fabs(mid), 0, 'f', 0)
                       .arg(mid > 0 ? "above" : "under");
    text += QString(" (80%: %1 to %2)").arg(signed_mm(above_lid(u.p10_oz)),
                                             signed_mm(above_lid(u.p90_oz)));
    text += mid > 0 ? QString(", which the lidder must press down.") : QString(".");
    if (defaulted && g.mound_slope_deg == BowlGeometry::kPlaceholderMoundSlope)
        text += QString(" <span style='color:%1'>%2 is a placeholder.</span>")
                    .arg(theme::palette().over.name(),
                         own ? "The default slope" : "Slope");
    return text;
}

DispenseVariation MainWindow::dispense() const
{
    DispenseVariation d;
    d.base = disp_base_->value() / 100.0;
    d.protein = disp_protein_->value() / 100.0;
    d.topping = disp_topping_->value() / 100.0;
    d.base_bias = bias_base_->value() / 100.0;
    d.protein_bias = bias_protein_->value() / 100.0;
    d.topping_bias = bias_topping_->value() / 100.0;
    return d;
}

BowlGeometry MainWindow::geometry() const
{
    BowlGeometry g;
    g.enabled = heights_->isChecked();
    g.depth_mm = depth_->value();
    g.top_diameter_mm = bowl_top_d_->value();
    g.bottom_diameter_mm = bowl_bottom_d_->value();
    g.lid_headroom_mm = headroom_->value();
    g.lid_plateau_diameter_mm = lid_plateau_d_->value();
    g.cups_pressed = cups_pressed_->isChecked();
    g.cup_height_mm = cup_h_->value();
    g.cup_diameter_mm = cup_d_->value();
    g.cup_base_diameter_mm = cup_base_d_->value();
    g.chunk_proud = chunk_proud_->value() / 100.0;
    g.mound = mound_->isChecked();
    g.mound_slope_deg = mound_slope_->value();
    return g;
}

void MainWindow::update_geometry_note(double capacity_oz, const SauceCups &sauce,
                                      const BowlGeometry &g, double chunk_mm)
{
    if (!g.enabled) {
        geometry_note_->setText(QString("Fit is judged by volume alone; each sauce cup "
                                        "counts as the %1 ml it takes up.")
                                    .arg(sauce.cup_ml, 0, 'f', 0));
        return;
    }
    SauceCups none = sauce;
    none.cups = 0;
    const double room = food_capacity_oz(capacity_oz, none, g, 0);
    const double after_chunks = food_capacity_oz(capacity_oz, none, g, chunk_mm);
    const double after_cups = food_capacity_oz(capacity_oz, sauce, g, chunk_mm);

    constexpr double kMlPerOz = 29.5735295625;
    const double fill_mm = g.height_of_mm3(capacity_oz * kMlPerOz * 1000.0);
    QString text = QString("Fill line %1 mm up a %2 mm bowl (%3 to the rim); lid at %4 mm. ")
                       .arg(fill_mm, 0, 'f', 1).arg(g.depth_mm, 0, 'f', 1)
                       .arg(units::volume(g.rim_ml() / kMlPerOz, true))
                       .arg(g.lid_mm(), 0, 'f', 1);
    if (sauce.cups > 0)
        text += QString("%1 %2 %3 take%4 <b>%5</b>. ")
                    .arg(sauce.cups).arg(sauce.cups == 1 ? "cup" : "cups")
                    .arg(g.cups_pressed ? "pressed in" : "resting on top")
                    .arg(sauce.cups == 1 ? "s" : "")
                    .arg(units::volume(after_chunks - after_cups, true));
    if (chunk_mm > 0 && room - after_chunks < 0.01)
        text += QString("The tallest chunk (%1 mm) clears the lid from the fill line. ")
                    .arg(chunk_mm, 0, 'f', 0);
    else if (chunk_mm > 0)
        text += QString("The tallest chunk (%1 mm) needs <b>%2</b> of clearance. ")
                    .arg(chunk_mm, 0, 'f', 0)
                    .arg(units::volume(room - after_chunks, true));
    text += QString("That leaves <b>%1</b> for food.").arg(units::volume(after_cups, true));

    // Say plainly which of these numbers are guesses.
    QStringList guesses;
    if (chunk_mm > 0 && g.chunk_proud == BowlGeometry::kPlaceholderChunkProud)
        guesses << "how proud chunks stand";
    if (const Menu *m = menu())
        for (const QString &n : bowl_names())
            if (const Ingredient *ing = m->find(n);
                ing && ing->piece_height_placeholder && ing->piece_height_mm == chunk_mm
                && chunk_mm > 0) {
                guesses << QString("%1's piece height").arg(n);
                break;
            }
    if (!guesses.isEmpty())
        text += QString("<br><span style='color:%1'>Placeholder: %2. Set real values in "
                        "Advanced.</span>")
                    .arg(theme::palette().over.name(), guesses.join(", "));
    geometry_note_->setText(text);
}

AdaptiveSettings MainWindow::adaptive_settings() const
{
    AdaptiveSettings a;
    a.bowl_capacity_oz = units::to_oz(capacity_->value());
    a.target_fill = target_fill_->value() / 100.0;
    a.band_low = band_low_->value() / 100.0;
    a.band_high = a.target_fill;
    a.tolerances.base = tol_base_->value() / 100.0;
    a.tolerances.protein = tol_protein_->value() / 100.0;
    a.tolerances.topping = tol_topping_->value() / 100.0;
    a.sauce.cups = sauce_cup_count();
    a.geometry = geometry();
    a.dispense = dispense();
    a.menu_price = menu_price_->value();
    a.cogs_target = cogs_target_->value() / 100.0;
    return a;
}

void MainWindow::recompute()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;

    if (adaptive_) {
        last_adaptive_ = solve_adaptive(assemble_bowl(), adaptive_settings(), &costs_);
        render_adaptive();
        return;
    }

    last_ = simulate(assemble_bowl(), legacy_settings());
    const SimResult &r = last_;
    update_fill_note(r);
    const SimSettings &settings = r.settings;   // carries the bowl's tallest chunk
    update_geometry_note(settings.bowl_capacity_oz, settings.sauce, settings.geometry,
                         settings.chunk_height_mm);
    const theme::Palette &pal = theme::palette();
    const double cap = settings.food_capacity_oz();
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
              + QString("As ordered it is %1 at %2 g, %3% of the bowl.")
                    .arg(units::volume(last.ounces, true)).arg(std::round(last.grams))
                    .arg(std::round(last.ounces / cap * 100));
        if (r.crossed_at_g) { accent_border = pal.over; accent_fill = pal.over_soft;
                              head_colour = pal.over; }
        break;
    case Verdict::FitsMassBound:
        head = "Mass limit binds first. The bowl fits.";
        sub = QString("Clears %1 g at %2 g and %3 — %4 under the %5 limit.")
                  .arg(std::round(settings.floor_g)).arg(std::round(last.grams))
                  .arg(units::volume(last.ounces, true),
                       units::volume(cap - last.ounces, true),
                       units::volume(cap, true));
        accent_border = pal.accent; accent_fill = pal.accent_soft; head_colour = pal.accent;
        break;
    case Verdict::Saturated:
        head = "Neither limit reached — the ingredients saturate first";
        sub = QString("Everything is at its max%1 the %2 g floor, at %3 with %4 spare.")
                  .arg(shortfall >= 1 ? QString(" %1 g short of").arg(std::round(shortfall))
                                      : QString(", landing on"))
                  .arg(std::round(settings.floor_g))
                  .arg(units::volume(last.ounces, true),
                       units::volume(cap - last.ounces, true));
        break;
    case Verdict::OverAtStart:
        head = "Over the bowl before the ramp starts";
        sub = QString("The ordered portions alone come to %1 at %2 g — past %3 with no "
                      "ramping at all. No weight floor can fix this; the portions "
                      "themselves have to come down.")
                  .arg(units::volume(r.first().ounces, true))
                  .arg(std::round(r.first().grams))
                  .arg(units::volume(cap, true));
        accent_border = pal.over; accent_fill = pal.over_soft; head_colour = pal.over;
        break;
    case Verdict::OverWhileRamping:
        head = "Volume limit binds first";
        sub = QString("The bowl passes %1 at %2 g, %3 g before the ramp stops at "
                      "%4 g. A floor near %5 would keep it inside the bowl.")
                  .arg(units::volume(cap, true))
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
    stat_value_[1]->setText(units::volume(last.ounces, true));
    stat_value_[2]->setText(QString("%1 %").arg(std::round(last.ounces / cap * 100)));
    stat_value_[3]->setText(units::volume(r.squeezed_oz(), true));
    stat_tile_[3]->setVisible(settings.compress);
    stat_value_[4]->setText(
        r.crossed_at_g ? (r.highest_safe_floor_g
                              ? QString("%1 g").arg(std::round(*r.highest_safe_floor_g))
                              : QString("none"))
                       : QString("no limit"));
    stat_value_[4]->setStyleSheet(QString("color:%1;").arg(pal.mass.name()));
    for (int i = 0; i < 5; ++i) stat_key_[i]->setText(kLegacyKeys[i]);

    // ---- breakdown --------------------------------------------------------
    double total_oz = 0;
    for (double oz : last.per_item_oz) total_oz += oz;
    if (total_oz <= 0) total_oz = 1;

    // The diagram goes first: the table is its legend, keyed by the colours it drew.
    diagram_->set_result(r);
    const std::vector<QColor> band = diagram_->item_colours();

    std::vector<BreakdownRow> rows;
    for (size_t i = 0; i < r.items.size(); ++i) {
        const BowlItem &it = r.items[i];
        const double added_g = it.final_g - it.start_g;
        const double added_oz = last.per_item_oz[i] - r.first().per_item_oz[i];
        const double squeeze =
            last.per_item_oz_uncompressed[i] - last.per_item_oz[i];

        BreakdownRow row;
        row.name = it.name;
        row.kind = QString("%1 · curve: %2")
                       .arg(to_string(it.kind),
                            describe(choose_curve(it.name, it.kind, curves_, method())));
        if (settings.compress && squeeze > 0.05)
            row.kind += QString(" · squeezed %1 by the load above it")
                            .arg(units::volume(squeeze, true));
        row.swatch = band[i];
        row.cells = {
            units::volume(last.per_item_oz[i], true),
            QString("%1%").arg(std::round(last.per_item_oz[i] / total_oz * 100)),
            QString("%1 g").arg(std::round(it.start_g)),
            QString("%1 g").arg(std::round(it.final_g)),
            added_g > 0.05 ? QString("+%1").arg(units::volume(added_oz, true)) : "—",
            units::rate(it.marginal_oz_per_100g(it.final_g)),
        };
        rows.push_back(row);
    }
    fill_breakdown(breakdown_,
                   {"Ingredient", "Volume", "Share", "Start", "Final", "Added",
                    "Rate"},
                   {{6, QString("Marginal %1 per +100 g at the final mass")
                            .arg(units::suffix())}},
                   rows);
    static_cast<BowlCard *>(bowl_row_)->relayout();

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
                           "volume per added gram that %3 does (%4 vs %5 %6 per +100 g). ")
                       .arg(worst->name).arg(fmt(ratio, 2)).arg(best->name)
                       .arg(units::rate(cost(worst)), units::rate(cost(best)),
                            units::suffix())
                 : QString("At the mass the ramp ends on, everything that ramps costs "
                           "about the same per added gram (%1 vs %2 %3 per +100 g). ")
                       .arg(units::rate(cost(best)), units::rate(cost(worst)),
                            units::suffix()))
            + scaling
            + QString(" Each pass costs %1 of bowl.")
                  .arg(units::volume(oz_per_pass, true)));
    } else if (ramped.size() == 1) {
        foot_note_->setText(QString("Only <b>%1</b> ramps: %2 of bowl spent to gain "
                                    "%3 g, over %4 passes.")
                                .arg(ramped[0]->name)
                                .arg(units::volume(
                                    last.per_item_oz[0] - r.first().per_item_oz[0], true))
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
            text = QString("%1 = %2·g^%3 · <b>%4</b> %1 per +100 g at %5 g<br>")
                       .arg(units::suffix())
                       .arg(fmt(units::scale_k(f->K), 3)).arg(fmt(f->p, 3))
                       .arg(units::rate(f->marginal_oz_per_100g(mid)))
                       .arg(std::round(mid));
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

    render_photos(r.items);

    ramp_chart_->set_show_uncompressed(settings.compress);
    ramp_chart_->set_result(r);
    curve_chart_->set_curves(&curves_, method());

    update_curve_note();
}

void MainWindow::update_curve_note()
{
    QStringList bits;
    for (const QString &n : curves_.ingredients())
        bits << QString("%1 (%2)").arg(n).arg(curves_.count(n, Method::Pooled));
    const int files = static_cast<int>(measurements_.files.size());
    QString text = QString("<b>%1</b> measurements from %2 file%3 in "
                           "<code>data/measurements</code>")
                       .arg(curves_.observations().size()).arg(files)
                       .arg(files == 1 ? "" : "s");
    text += bits.isEmpty() ? QString(".") : QString(": %1.").arg(bits.join(", "));
    const Backfill &bf = measurements_.backfill;
    if (bf.valid)
        text += QString(" Backfilled from %1 (%2 readings): ×%3 at 10 ml, ×%4 at 50 ml, "
                        "×%5 at 200 ml.")
                    .arg(bf.reference).arg(bf.points)
                    .arg(bf.factor_at_ml(10), 0, 'f', 2).arg(bf.factor_at_ml(50), 0, 'f', 2)
                    .arg(bf.factor_at_ml(200), 0, 'f', 2);
    else if (measurements_.backfill_requested)
        text += QString(" <span style='color:%1'>Backfill is on but there is no usable "
                        "\"test ...\" ladder (%2 readings; %3 needed).</span>")
                    .arg(theme::palette().over.name()).arg(bf.points).arg(kMinReferencePoints);
    if (const int n = measurements_.dropped_total()) {
        QStringList parts;
        for (const auto &[name, count] : measurements_.dropped)
            parts << QString("%1 %2").arg(name).arg(count);
        text += QString(" Ignored %1 impossible reading%2 (%3).")
                    .arg(n).arg(n == 1 ? "" : "s").arg(parts.join(", "));
    }
    // Can the 80% ranges be trusted? Predict each scanned mixed bowl from the single-
    // ingredient curves and see how often the scan lands inside its range.
    const RangeCheck rc = check_ranges(measurements_.bowls, curves_, method(),
                                       compress_->isChecked() ? load_transfer_->value() : 0.0);
    if (rc.bowls) {
        const double share = rc.inside_share();
        const bool off = share < 0.65 || share > 0.95 || rc.above > 2 * rc.below + 3
                         || rc.below > 2 * rc.above + 3;
        text += QString(" <span style='color:%1'>Range check: %2 of %3 mixed bowls (%4%) "
                        "landed inside their predicted 80% range (%5 above, %6 below)%7.</span>")
                    .arg((off ? theme::palette().over : theme::palette().ink_soft).name())
                    .arg(rc.inside).arg(rc.bowls).arg(std::round(share * 100))
                    .arg(rc.above).arg(rc.below)
                    .arg(off ? "; the overfill odds are not yet trustworthy" : "");
    }
    if (squash_.valid)
        text += QString(" Squash fit from %1 mixed bowl%2 (not applied): load transfer %3%4.")
                    .arg(squash_.bowls).arg(squash_.bowls == 1 ? "" : "s")
                    .arg(squash_.load_transfer, 0, 'f', 2)
                    .arg(squash_.at_bound ? ", at its limit" : "");
    QStringList bad;
    for (const MeasurementFile &f : measurements_.files)
        if (f.failed) bad << f.name;
    if (measurements_.folder_missing || bad.size())
        text += QString(" <span style='color:%1'>%2</span>")
                    .arg(theme::palette().over.name(),
                         measurements_.folder_missing
                             ? QString("The folder does not exist yet; upload a file to create it.")
                             : QString("Not loaded: %1.").arg(bad.join(", ")));
    curve_note_->setText(text);
}

//
// ############################################################################
// CSV ingest
//

void MainWindow::show_report()
{
    const Menu *m = menu();
    if (!m) return;

    // Each recipe resolves its own floor, so the panel's floor field is deliberately
    // not carried in: the report answers what the menu does, not what is dialled up.
    const BrandAudit audit =
        audit_brand(*m, curves_, method(), legacy_settings(), adaptive_settings(), &costs_);

    const QString assumptions =
        QString("%1 bowl · %2 curves · %3 sauce cup%4 charged against capacity in both "
                "models · adaptive target %5% · each recipe uses the floor its own "
                "ingredients match.")
            .arg(units::volume(units::to_oz(capacity_->value()), true))
            .arg(method() == Method::Robot ? "robot" : method() == Method::Hand ? "hand" : "pooled")
            .arg(sauce_cup_count())
            .arg(sauce_cup_count() == 1 ? "" : "s")
            .arg(std::round(target_fill_->value()));

    ReportDialog dialog(audit, assumptions, this);
    connect(&dialog, &ReportDialog::recipe_chosen, this,
            [this, m](const QString &recipe) { select(m->brand, recipe); });
    connect(&dialog, &ReportDialog::full_report_requested, this, [this, m] {
        SweepContext ctx;
        ctx.menus = &menus_;
        ctx.brand = m->brand;
        ctx.curves = &curves_;
        ctx.method = method();
        ctx.legacy = legacy_settings();
        ctx.adaptive = adaptive_settings();
        ctx.costs = &costs_;
        SweepDialog(std::move(ctx), this).exec();
    });
    dialog.exec();
}

void MainWindow::show_csv_help()
{
    QMessageBox box(this);
    box.setWindowTitle("Mass-to-volume CSV format");
    box.setTextFormat(Qt::RichText);
    box.setText(CurveSet::format_help());
    box.setIcon(QMessageBox::Information);
    box.exec();
}

QString MainWindow::measurements_dir() const
{
    return asset_dir_ + kMeasurementsDir;
}

QStringList MainWindow::reload_measurements()
{
    MeasurementOptions options;
    options.empty_bowl_ml = empty_bowl_ml_->value();
    options.backfill = backfill_->isChecked();
    options.drop_impossible = drop_impossible_->isChecked();
    measurements_ = load_measurements(measurements_dir(), curves_, options);
    // Fitted for information only, never applied: the model can only squash bases
    // whose own curve compacts (p < 1), so with camera-fitted curves the fit often
    // rests on one or two bowls. Compression and load transfer stay as set.
    squash_ = fit_squash(measurements_.bowls, curves_, method());
    QStringList problems;
    if (measurements_.folder_missing)
        problems << QString("data/measurements does not exist, so no curves are loaded. "
                            "Upload a file to create it.");
    for (const MeasurementFile &f : measurements_.files)
        if (f.failed) problems << QString("data/measurements/%1: %2").arg(f.name, f.problem);
    if (curve_note_) update_curve_note();
    // A scaled curve keeps its scale; the rate it shows follows the new fit.
    for (const auto &[name, field] : rate_fields_) refresh_rate_field(name);
    return problems;
}

/// Copies an accepted upload into the store and reloads it, reporting where it went.
QString MainWindow::store_and_reload(const QString &path, QStringList *problems)
{
    QString err;
    const QString stored = store_measurement_file(path, measurements_dir(), &err);
    if (stored.isEmpty()) {
        QMessageBox::warning(this, "Could not save the file", err);
        return {};
    }
    *problems = reload_measurements();
    return QFileInfo(stored).fileName();
}

void MainWindow::on_upload_csv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Add mass-to-volume measurements", asset_dir_ + "/data",
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    // Read it on its own first, so a bad file is refused before it is stored.
    CurveSet check;
    QString note;
    int added = 0;
    if (!check.load_csv(path, &note, &added)) {
        QMessageBox box(this);
        box.setWindowTitle("Could not read that CSV");
        box.setIcon(QMessageBox::Warning);
        box.setText(note);
        box.setInformativeText(is_curve_csv(path)
                                   ? "Click Show Details for the expected format."
                                   : "This looks like a bowl sheet (a column per "
                                     "ingredient); use Upload bowl data instead.");
        box.setDetailedText(QString(CurveSet::format_help())
                                .replace(QRegularExpression("<[^>]+>"), ""));
        box.exec();
        return;
    }

    const QStringList before = curves_.ingredients();
    QStringList problems;
    const QString stored = store_and_reload(path, &problems);
    if (stored.isEmpty()) return;
    QStringList fresh;
    for (const QString &n : curves_.ingredients())
        if (!before.contains(n)) fresh << n;

    QString msg = QString("Saved as data/measurements/%1 and loaded: %2 row%3. ")
                      .arg(stored).arg(added).arg(added == 1 ? "" : "s");
    if (!fresh.isEmpty())
        msg += QString("New curve%1 for %2. ")
                   .arg(fresh.size() == 1 ? "" : "s", fresh.join(", "));
    if (!note.isEmpty()) msg += note;
    if (!problems.isEmpty()) msg += "\n\nOther files not loaded:\n" + problems.join("\n");
    QMessageBox::information(this, "Measurements added", msg);

    recompute();
}

void MainWindow::show_bowl_help()
{
    QMessageBox box(this);
    box.setWindowTitle("Bowl data CSV format");
    box.setTextFormat(Qt::RichText);
    box.setText(bowl_data_format_help());
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::on_upload_bowl_data()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Add scanned bowls", asset_dir_ + "/data", "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    BowlSheet sheet;
    QString err;
    if (!load_bowl_csv(path, sheet, &err, empty_bowl_ml_->value())) {
        QMessageBox box(this);
        box.setWindowTitle("Could not read that CSV");
        box.setIcon(QMessageBox::Warning);
        box.setText(err);
        box.setInformativeText("Click Show Details for the expected format.");
        box.setDetailedText(bowl_data_format_help().replace(QRegularExpression("<[^>]+>"), ""));
        box.exec();
        return;
    }

    // Store it, then reload everything: the curves refit with this sheet's single-
    // ingredient bowls, and the squash is fitted over every mixed bowl on file.
    const QStringList before = curves_.ingredients();
    const std::vector<Observation> points = single_ingredient_points(sheet);
    QStringList problems;
    const QString stored = store_and_reload(path, &problems);
    if (stored.isEmpty()) return;
    QStringList fresh;
    for (const QString &n : curves_.ingredients())
        if (!before.contains(n)) fresh << n;

    QString msg = QString("Saved as <code>data/measurements/%1</code> and loaded.<br><br>"
                          "Read %2 bowl%3: %4 single-ingredient, %5 mixed.")
                      .arg(stored).arg(sheet.rows.size()).arg(sheet.rows.size() == 1 ? "" : "s")
                      .arg(sheet.single_rows()).arg(sheet.mixed_rows());
    if (sheet.skipped)
        msg += QString(" %1 row%2 skipped (no volume or no weights).")
                   .arg(sheet.skipped).arg(sheet.skipped == 1 ? "" : "s");
    msg += QString("<br><br>%1 curve point%2 added")
               .arg(points.size()).arg(points.size() == 1 ? "" : "s");
    msg += fresh.isEmpty() ? "." : QString("; new curve%1 for %2.")
                                       .arg(fresh.size() == 1 ? "" : "s", fresh.join(", "));

    const SquashFit &fit = squash_;   // over every mixed bowl on file; reported, not applied
    if (fit.valid) {
        const double ml = CurveSet::kMlPerFlOz;
        msg += QString("<br><br><b>Squash fit from all %1 mixed bowl%2 on file (not applied): "
                       "load transfer %3.</b><br>Typical error: %4 ml with no squash, %5 ml at 1.0, "
                       "<b>%6 ml</b> fitted (bias %7 ml).")
                   .arg(fit.bowls).arg(fit.bowls == 1 ? "" : "s")
                   .arg(fit.load_transfer, 0, 'f', 2)
                   .arg(std::round(fit.rmse_uncompressed_oz * ml))
                   .arg(std::round(fit.rmse_default_oz * ml))
                   .arg(std::round(fit.rmse_fitted_oz * ml))
                   .arg(std::round(fit.mean_bias_oz * ml));
        if (fit.at_bound)
            msg += QString("<br><span style='color:%1'>The fit hit its limit of %2, so its "
                           "value means nothing: the model can only squash bases whose own "
                           "curve compacts, and most of these bowls have none.</span>")
                       .arg(theme::palette().over.name()).arg(kMaxLoadTransfer);
        if (!fit.flat_rate.isEmpty())
            msg += QString("<br>No curve yet, carried at a flat rate: %1.")
                       .arg(fit.flat_rate.join(", "));
        msg += "<br><br>Compression and load transfer are unchanged; set them in Advanced.";
    }
    if (!problems.isEmpty())
        msg += "<br><br>Other files not loaded:<br>" + problems.join("<br>");

    QMessageBox box(this);
    box.setWindowTitle("Bowl data added");
    box.setTextFormat(Qt::RichText);
    box.setText(msg);
    box.setIcon(QMessageBox::Information);
    box.exec();
    recompute();
}

void MainWindow::on_reset_curves()
{
    const QStringList problems = reload_measurements();
    if (!problems.isEmpty())
        QMessageBox::warning(this, "Some measurement files could not be loaded",
                             problems.join("\n"));
    recompute();
}


//
// ############################################################################
// Adaptive Dispense rendering
//

///
/// Shared by both models: the reference photo nearest each base's dispensed weight.
///
void MainWindow::render_photos(const std::vector<BowlItem> &items)
{
    const theme::Palette &pal = theme::palette();
    // Photo series by family word, so renamed bases still find theirs: every rice
    // (White Jasmine Rice, White Rice, Brown Rice and Lentils, ...) shows the Mexican
    // rice series, the only rice photographed.
    static const std::pair<QRegularExpression, QString> kPhotoSeries[] = {
        {QRegularExpression(R"(\bromaine\b)", QRegularExpression::CaseInsensitiveOption), "Romaine"},
        {QRegularExpression(R"(\bkale\b)", QRegularExpression::CaseInsensitiveOption), "Kale"},
        {QRegularExpression(R"(\brice\b)", QRegularExpression::CaseInsensitiveOption), "MexicanRice"}};
    QDir photo_dir(asset_dir_ + "/photos");
    int slot = 0;
    for (const BowlItem &it : items) {
        if (it.kind != Kind::Base || slot > 1) continue;
        const auto pit = std::find_if(std::begin(kPhotoSeries), std::end(kPhotoSeries),
                                      [&](const auto &s) { return it.name.contains(s.first); });
        if (pit == std::end(kPhotoSeries)) continue;
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
        photo_[slot]->set_source(pm);
        photo_[slot]->setVisible(true);
        photo_caption_[slot]->setVisible(true);
        photo_caption_[slot]->setText(
            QString("<b>%1</b><br>%2 g simulated · %3 g photo%4")
                .arg(it.name).arg(std::round(it.final_g)).arg(best, 0, 'f', 0)
                .arg(pit->second == "MexicanRice"
                             && it.name.compare("Mexican Rice", Qt::CaseInsensitive) != 0
                         ? "<br><span style='color:" + pal.over.name()
                               + "'>proxy series</span>"
                         : ""));
        ++slot;
    }
    for (int i = slot; i < 2; ++i) {
        // An empty slot collapses, so a single photo centres under the bowl.
        photo_[i]->set_source(QPixmap());
        photo_[i]->setVisible(false);
        photo_caption_[i]->setText(i == 0 ? "No measured photo series for this base." : "");
        photo_caption_[i]->setVisible(i == 0);
    }
    static_cast<BowlCard *>(bowl_row_)->relayout();   // a photo more or less changes the fit
}

void MainWindow::render_adaptive()
{
    const AdaptiveResult &r = last_adaptive_;
    const AdaptiveSettings &a = r.settings;
    const theme::Palette &pal = theme::palette();
    const double cap = a.food_capacity_oz();   // the target is a share of this

    QString head, sub;
    QColor border = pal.rule_strong, fill = pal.panel_sunk, head_colour = pal.ink;
    switch (r.status) {
    case AdaptiveStatus::NoAdjustment:
        head = "No adjustment required";
        sub = QString("The recipe as written lands at %1 — %2% of the food room, inside the "
                      "%3–%4% band — so nominal quantities are dispensed unchanged.")
                  .arg(units::volume(r.food_volume_oz, true))
                  .arg(std::round(r.food_fill() * 100))
                  .arg(std::round(a.band_low * 100))
                  .arg(std::round(a.band_high * 100));
        border = pal.accent; fill = pal.accent_soft; head_colour = pal.accent;
        break;
    case AdaptiveStatus::Adjusted:
        head = QString("Adjusted to target — every ingredient moved %1% of its band")
                   .arg(std::fabs(r.alpha) * 100, 0, 'f', 0);
        sub = QString("Solved to %1, %2% of the food room. %3 %4 of its allowance; "
                      "protein moved least by design.")
                  .arg(units::volume(r.food_volume_oz, true))
                  .arg(std::round(r.food_fill() * 100))
                  .arg(r.alpha < 0 ? "Reduced by" : "Increased by")
                  .arg(QString("%1%").arg(std::fabs(r.alpha) * 100, 0, 'f', 0));
        border = pal.accent; fill = pal.accent_soft; head_colour = pal.accent;
        break;
    case AdaptiveStatus::StillOver:
        head = "Will not fit, even at full reduction";
        sub = QString("Every ingredient is at the bottom of its tolerance and the bowl "
                      "still comes to %1 against a %2 target. The bands are not wide "
                      "enough to rescue this recipe — the nominal spec has to change.")
                  .arg(units::volume(r.food_volume_oz, true))
                  .arg(units::volume(a.target_fill * cap, true));
        border = pal.over; fill = pal.over_soft; head_colour = pal.over;
        break;
    case AdaptiveStatus::Underfilled:
        head = "Underfilled — cannot reach the target";
        sub = QString("Every ingredient is at the top of its tolerance and the bowl "
                      "only reaches %1 against a %2 target. Dispensed safely short and "
                      "flagged, per US-6.")
                  .arg(units::volume(r.food_volume_oz, true))
                  .arg(units::volume(a.target_fill * cap, true));
        break;
    case AdaptiveStatus::NoIngredients:
        head = "Nothing in the bowl";
        break;
    }
    verdict_head_->setText(head);
    verdict_head_->setStyleSheet(QString("color:%1;").arg(head_colour.name()));
    verdict_sub_->setText(sub);
    verdict_panel_->setStyleSheet(
        QString("QFrame#panel{background:%1;border:1px solid %2;border-radius:10px;}")
            .arg(fill.name(), border.name()));

    // Tiles: food, occupancy including the cups, and COGS against the guardrail.
    stat_value_[0]->setText(QString("%1 g").arg(
        std::round(std::accumulate(r.items.begin(), r.items.end(), 0.0,
                                   [](double t, const AdaptiveItem &i) {
                                       return t + i.final_g;
                                   }))));
    stat_value_[1]->setText(units::volume(r.food_volume_oz, true));
    stat_value_[2]->setText(QString("%1 %").arg(std::round(r.food_fill() * 100)));
    stat_tile_[3]->setVisible(true);
    stat_value_[3]->setText(QString("%1 %").arg(std::round(r.occupancy() * 100)));
    const bool ok = r.cogs_within_guardrail();
    stat_value_[4]->setText(a.menu_price > 0
                                ? QString("%1 %").arg(r.cogs_ratio() * 100, 0, 'f', 1)
                                : QString("—"));
    stat_value_[4]->setStyleSheet(
        QString("color:%1;").arg((ok ? pal.accent : pal.over).name()));

    // The diagram and photos still describe the dispensed bowl, so feed them a
    // SimResult built from the solved quantities.
    SimResult shim;
    shim.settings.bowl_capacity_oz = a.bowl_capacity_oz;
    shim.settings.sauce = a.sauce;
    shim.settings.geometry = a.geometry;
    shim.settings.chunk_height_mm = a.chunk_height_mm;
    shim.items = assemble_bowl();
    Frame f;
    for (size_t i = 0; i < shim.items.size() && i < r.items.size(); ++i) {
        shim.items[i].final_g = r.items[i].final_g;
        f.per_item_oz.push_back(r.items[i].volume_oz);
        f.per_item_oz_uncompressed.push_back(r.items[i].volume_oz);
        f.grams += r.items[i].final_g;
        f.ounces += r.items[i].volume_oz;
    }
    f.ounces_uncompressed = f.ounces;
    shim.frames.push_back(f);
    shim.fill = r.fill;
    update_fill_note(shim);
    diagram_->set_result(shim);
    render_photos(shim.items);
    const std::vector<QColor> band = diagram_->item_colours();

    // Breakdown, in the same leading columns but reading nominal -> dispensed.
    std::vector<BreakdownRow> rows;
    const double food_oz = std::max(0.001, r.food_volume_oz);
    for (size_t i = 0; i < r.items.size(); ++i) {
        const AdaptiveItem &it = r.items[i];
        BreakdownRow row;
        row.name = it.name;
        if (it.clamped) row.name += "  (clamped)";
        if (!it.cost_known) row.name += "  (~cost)";
        row.kind = QString("%1 · curve: %2")
                       .arg(to_string(it.kind),
                            describe(choose_curve(it.name, it.kind, curves_, method())));
        row.swatch = i < band.size() ? band[i] : QColor();
        if (it.clamped) row.colour = pal.over;
        row.cells = {
            units::volume(it.volume_oz, true),
            QString("%1%").arg(std::round(it.volume_oz / food_oz * 100)),
            QString("%1 g").arg(std::round(it.nominal_g)),
            QString("%1 g").arg(std::round(it.final_g)),
            QString("%1%2%").arg(it.delta_pct() > 0.05 ? "+" : "")
                            .arg(it.delta_pct(), 0, 'f', 1),
            QString("$%1").arg(it.cost, 0, 'f', 2),
        };
        rows.push_back(row);
    }
    fill_breakdown(breakdown_,
                   {"Ingredient", "Volume", "Share", "Nominal", "Dispensed", "Change",
                    "Cost"},
                   {{5, "Dispensed against nominal, inside the ingredient's tolerance"}},
                   rows);
    static_cast<BowlCard *>(bowl_row_)->relayout();

    update_geometry_note(a.bowl_capacity_oz, a.sauce, a.geometry, a.chunk_height_mm);
    QString note = QString("%1 take %2 of the bowl, leaving %3 for food. ")
                       .arg(a.geometry.enabled ? "Cups and chunk clearance" : "Sauce cups")
                       .arg(units::volume(a.sauce_volume_oz(), true))
                       .arg(units::volume(a.food_capacity_oz(), true));
    if (a.menu_price > 0)
        note += QString("COGS is <b>%1%</b> of $%2 against a %3% guardrail — <b>%4</b>. ")
                    .arg(r.cogs_ratio() * 100, 0, 'f', 1)
                    .arg(a.menu_price, 0, 'f', 2)
                    .arg(std::round(a.cogs_target * 100))
                    .arg(ok ? "within" : "over");
    if (!r.cost_complete)
        note += QString("<span style='color:%1'>Some ingredients fell back to a "
                        "per-kind default price.</span> ").arg(pal.over.name());
    note += "All prices in <code>data/ingredient_costs.csv</code> are placeholders.";
    foot_note_->setText(note);
    adaptive_note_->setText(
        QString("Tolerances are one requirement stated twice: %1:%2:%3 is the "
                "\"4:1:2 ratio steps\" from the document. Volume is solved for; COGS is "
                "reported, never optimised.")
            .arg(std::round(tol_base_->value()))
            .arg(std::round(tol_protein_->value()))
            .arg(std::round(tol_topping_->value())));

    tol_chart_->set_result(r);


    curve_chart_->set_curves(&curves_, method());
}

}  // namespace bowlfill
