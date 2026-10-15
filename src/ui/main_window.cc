#include "main_window.hh"

#include "core/bowl_data.hh"
#include "core/manual_entry.hh"
#include "core/measurements.hh"
#include "core/step_table.hh"

#include "bowl_diagram.hh"
#include "manual_entry_settings.hh"
#include "qt_text.hh"
#include "chip_picker.hh"
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
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
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
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QDateTime>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
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
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <optional>
#include <utility>

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

QDoubleSpinBox *make_spin(double lo, double hi, double step, int decimals)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false);
    theme::bare_spin(s);
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

QString capitalised(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

/// Item data roles in the finished-bowl table.
constexpr int kFieldRole = Qt::UserRole;        ///< Field, on an editable cell
constexpr int kValueRole = Qt::UserRole + 1;    ///< the number its editor starts from
constexpr int kNameRole = Qt::UserRole + 2;     ///< the ingredient, on the name cell

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
/// Edits a portion cell in place with a number box. The commit is handed to the
/// window, which records the edit and recomputes; the table itself is rebuilt from
/// the result, so the delegate never writes the cell.
///
class PortionDelegate : public QStyledItemDelegate {
public:
    using Commit = std::function<void(int row, int column, double value)>;
    explicit PortionDelegate(Commit commit, QObject *parent)
        : QStyledItemDelegate(parent), commit_(std::move(commit)) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        const auto field = static_cast<Field>(index.data(kFieldRole).toInt());
        if (field == Field::None) return nullptr;
        auto *spin = new QDoubleSpinBox(parent);
        theme::bare_spin(spin);
        spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        spin->setRange(0, field == Field::Rate ? 6000 : field == Field::Step ? 100 : 2000);
        spin->setDecimals(field == Field::Step ? 2 : field == Field::Rate ? 1 : 0);
        spin->setSingleStep(field == Field::Step ? 0.1 : 1);
        return spin;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *spin = static_cast<QDoubleSpinBox *>(editor);
        spin->setValue(index.data(kValueRole).toDouble());
        spin->selectAll();
    }
    void setModelData(QWidget *editor, QAbstractItemModel *,
                      const QModelIndex &index) const override
    {
        auto *spin = static_cast<QDoubleSpinBox *>(editor);
        spin->interpretText();
        // Leaving a cell untouched is not an edit: it must not pin the menu's value.
        if (std::fabs(spin->value() - index.data(kValueRole).toDouble()) < 1e-9) return;
        commit_(index.row(), index.column(), spin->value());
    }
    // An editable cell looks like an input: a sunk, outlined box behind its number.
    // Drawn here because the stylesheet's ::item rule overrides item backgrounds.
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (static_cast<Field>(index.data(kFieldRole).toInt()) != Field::None) {
            const theme::Palette &pal = theme::palette();
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(pal.rule, 1));
            painter->setBrush(pal.panel_sunk);
            painter->drawRoundedRect(QRectF(option.rect).adjusted(4.5, 5.5, -2.5, -5.5), 4, 4);
            painter->restore();
        }
        QStyledItemDelegate::paint(painter, option, index);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect.adjusted(2, 4, -2, -4));
    }

private:
    Commit commit_;
};

///
/// Fills the finished-bowl table, which doubles as the bowl diagram's legend (each
/// name carries the swatch of its band) and is where a chef edits portions: cells
/// with a field are editable, tinted, and show the user's own values in the accent.
/// The last column is a per-row reset. The table is sized to its rows so the page
/// scrolls as one, rather than the table scrolling inside the page.
///
void fill_breakdown(QTableWidget *table, const QStringList &headers,
                    const std::map<int, QString> &header_tips,
                    const std::vector<BreakdownRow> &rows)
{
    const theme::Palette &pal = theme::palette();
    const QSignalBlocker block(table);
    const int columns = headers.size() + 1;   // and the reset column
    table->setColumnCount(columns);
    QStringList all = headers;
    all << QString();
    table->setHorizontalHeaderLabels(all);
    table->setRowCount(static_cast<int>(rows.size()));
    table->setIconSize(QSize(14, 14));
    for (int c = 0; c < headers.size(); ++c)
        if (QTableWidgetItem *h = table->horizontalHeaderItem(c))
            h->setTextAlignment((c == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
    for (const auto &[c, tip] : header_tips)
        if (QTableWidgetItem *h = table->horizontalHeaderItem(c)) h->setToolTip(tip);
    QFont edited_font = theme::font(theme::Text::Body, true);
    QColor editable_bg = pal.panel_sunk;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const BreakdownRow &row = rows[i];
        auto *name = new QTableWidgetItem(swatch_icon(row.swatch), row.name);
        name->setToolTip(row.kind);
        name->setData(kNameRole, row.key);
        name->setFlags(Qt::ItemIsEnabled);
        if (row.colour.isValid()) name->setForeground(row.colour);
        table->setItem(i, 0, name);
        for (size_t c = 0; c < row.cells.size(); ++c) {
            const TableCell &cell = row.cells[c];
            auto *v = new QTableWidgetItem(cell.text);
            v->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            v->setData(kFieldRole, static_cast<int>(cell.field));
            if (cell.field == Field::None) {
                v->setFlags(Qt::ItemIsEnabled);
            } else {
                v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
                v->setData(kValueRole, cell.value);
                v->setBackground(editable_bg);
                v->setToolTip(cell.edited ? "Your value. Click to change it; ↺ resets "
                                            "the row to the menu."
                                          : "Click to edit.");
                if (cell.edited) {
                    v->setForeground(pal.accent);
                    v->setFont(edited_font);
                }
            }
            table->setItem(i, static_cast<int>(c) + 1, v);
        }
        auto *reset = new QTableWidgetItem(row.edited ? QString::fromUtf8("↺") : QString());
        reset->setFlags(Qt::ItemIsEnabled);
        reset->setTextAlignment(Qt::AlignCenter);
        reset->setForeground(pal.ink_soft);
        if (row.edited) reset->setToolTip("Reset this ingredient to the menu's values");
        table->setItem(i, columns - 1, reset);
    }
    // Numeric columns are sized by hand: resizeColumnsToContents() reserves room in
    // each header for a sort arrow the table never shows, and in a narrow window that
    // slack is taken from the ingredient names. Editable columns get room for a box.
    const QFontMetrics body(theme::font(theme::Text::Body, true));
    const QFontMetrics head(theme::font(theme::Text::Caption, true));
    for (int c = 1; c < headers.size(); ++c) {
        int w = head.horizontalAdvance(headers[c]);
        bool editable = false;
        for (int i = 0; i < table->rowCount(); ++i)
            if (const QTableWidgetItem *it = table->item(i, c)) {
                w = std::max(w, body.horizontalAdvance(it->text()));
                editable |= it->flags().testFlag(Qt::ItemIsEditable);
            }
        table->setColumnWidth(c, std::max(w + theme::space::lg + theme::space::sm,
                                          editable ? 64 : 0));
    }
    table->setColumnWidth(columns - 1, 28);
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
///   stacked  bowl beside photos, table below
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
            // The bowl and its photos share the top row, so neither sits beside dead
            // space; the table, the widest thing on the card, takes the row below.
            grid_->addWidget(left_, 0, 0, Qt::AlignTop);
            grid_->addWidget(photos_, 0, 1, Qt::AlignTop | Qt::AlignLeft);
            grid_->addWidget(table_, 1, 0, 1, 3);
            grid_->setColumnStretch(2, 1);
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
    std::string map_err;
    if (!load_name_mappings(ss(mappings_path()), name_maps_, &map_err))
        load_warnings_ << qs(map_err);
    if (!load_piece_heights(ss(pieces_path()), pieces_, &map_err)) load_warnings_ << qs(map_err);
    if (!load_mound_slopes(ss(mounds_path()), mound_slopes_, &map_err)) load_warnings_ << qs(map_err);
    if (!load_roll_off(ss(rolloff_path()), roll_off_, &map_err)) load_warnings_ << qs(map_err);
    LoadResult loaded = load_menus(ss(asset_dir_ + "/menus"), &name_maps_, &pieces_);
    menus_ = std::move(loaded.menus);
    load_warnings_ << qsl(loaded.warnings);
    menu_issues_ = qsl(loaded.issues);
    manual_ = load_manual_entry();
    rebuild_manual_menu();

    std::string cost_err;
    if (!costs_.load_csv(ss(asset_dir_ + "/data/ingredient_costs.csv"), &cost_err))
        load_warnings_ << QString("ingredient_costs.csv: %1").arg(qs(cost_err));

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
    mark_settings();
    if (prefs.value("ui/settings_open", false).toBool()) {
        // Open as it was left, without the slide.
        const QSignalBlocker block(settings_);
        settings_->setChecked(true);
        body_->setCurrentIndex(1);
    }

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

    // Two views of the window body: the recipe beside its results, and the settings,
    // which take the whole width when open so no control is cramped. A line at the
    // top of the settings keeps the current bowl in view while they are tuned.
    body_ = new QStackedWidget;
    body_->setObjectName("clear");
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(build_controls());
    splitter->addWidget(build_results());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({380, 1060});
    body_->addWidget(splitter);
    body_->addWidget(build_settings());
    root->addWidget(body_, 1);

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
    mode_legacy_ = make_toggle("Dynamic portioning");
    mode_adaptive_ = make_toggle("Adaptive fill");
    mode_legacy_->setChecked(true);
    mode_legacy_->setToolTip("What runs today (dynamic_portion_algorithm.cc): every "
                             "ingredient is topped up in small steps until the bowl "
                             "clears its weight floor.");
    mode_adaptive_->setToolTip("The proposed replacement: one dial moves every ingredient "
                               "inside its allowance so the food lands on a fill level.");
    connect(mode_legacy_, &QPushButton::clicked, this, [this] { set_mode(false); });
    connect(mode_adaptive_, &QPushButton::clicked, this, [this] { set_mode(true); });
    mode_volume_ = make_toggle("Volume fill");
    mode_volume_->setToolTip("The ramp steered by volume alone, whatever the weight floor: "
                             "tops up while the next step still fits, and steps bases and "
                             "toppings down (never proteins) when the bowl is over.");
    connect(mode_volume_, &QPushButton::clicked, this, [this] { set_model(Model::VolumeFill); });
    auto *modes = segment({mode_legacy_, mode_adaptive_, mode_volume_});
    modes->setMinimumWidth(440);
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

    estimates_ = new QPushButton;
    estimates_->setObjectName("estimates");
    estimates_->setCursor(Qt::PointingHandCursor);
    estimates_->setVisible(false);
    connect(estimates_, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        QAction *title = menu.addAction("This bowl rests on these estimates:");
        title->setEnabled(false);
        menu.addSeparator();
        for (const Estimate &e : estimates_list_) {
            QAction *a = menu.addAction(e.text);
            if (e.go) connect(a, &QAction::triggered, this, e.go);
            else a->setEnabled(false);
        }
        menu.exec(estimates_->mapToGlobal(QPoint(0, estimates_->height())));
    });
    h->addWidget(estimates_);

    auto *report = new QPushButton("Brand report…");
    report->setToolTip("Every recipe this brand defines, checked against the current bowl "
                       "and curves under both dispense models, with a full sweep of every "
                       "combination.");
    connect(report, &QPushButton::clicked, this, &MainWindow::show_report);
    h->addWidget(report);

    settings_ = make_toggle("⚙  Settings");
    settings_->setToolTip("Bowl and lid, dispense variation, mounding, compression, "
                          "measurement data and ingredient names.");
    connect(settings_, &QPushButton::toggled, this, [this](bool on) { open_settings(on); });
    h->addWidget(settings_);

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
    scroll->setMinimumWidth(360);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *host = new QWidget;
    host->setObjectName("clear");
    auto *col = new QVBoxLayout(host);
    // A panel border sitting exactly on the scroll area's edge can lose its outer
    // pixel column to rounding at fractional display scales, which shows on the
    // coloured verdict border. A small inset keeps every border inside the viewport.
    col->setContentsMargins(kEdgeInset, kEdgeInset, space::md, kEdgeInset);
    col->setSpacing(space::md);

    // ---- the recipe: what goes in the bowl. Everything a chef plans with is here
    // or in the finished-bowl table; the model's settings are in the drawer.
    QVBoxLayout *v = nullptr;
    col->addWidget(titled_panel("Recipe", v));

    brand_ = make_combo();
    connect(brand_, &QComboBox::currentIndexChanged, this, &MainWindow::on_brand_changed);
    v->addWidget(field("Brand", brand_));

    recipe_ = make_combo();
    connect(recipe_, &QComboBox::currentIndexChanged, this, &MainWindow::on_recipe_changed);
    v->addWidget(field("Recipe", recipe_));
    recipe_note_ = make_label("", "note");
    v->addWidget(recipe_note_);

    // Saved scenarios: this brand's named set-ups, each a recipe with its picks and
    // edits, brought back whole.
    auto *scenario_row = new QHBoxLayout;
    scenario_row->setSpacing(space::sm);
    scenario_ = make_combo();
    scenario_->setToolTip("Bring back a saved set-up of this brand: recipe, ingredients, "
                          "portion edits and floor. Saved in data/scenarios.");
    connect(scenario_, &QComboBox::activated, this, [this](int i) {
        if (i > 0 && i - 1 < static_cast<int>(scenarios_.size())) apply_scenario(scenarios_[i - 1]);
        scenario_delete_->setEnabled(i > 0);
    });
    scenario_row->addWidget(field("Saved scenario", scenario_), 1);
    auto *save = new QPushButton("Save\u2026");
    save->setToolTip("Save the bowl as it is now, edits and all, under a name.");
    connect(save, &QPushButton::clicked, this, &MainWindow::save_scenario);
    scenario_row->addWidget(save, 0, Qt::AlignBottom);
    scenario_delete_ = new QPushButton("Delete");
    scenario_delete_->setObjectName("quiet");
    scenario_delete_->setEnabled(false);
    connect(scenario_delete_, &QPushButton::clicked, this, &MainWindow::delete_scenario);
    scenario_row->addWidget(scenario_delete_, 0, Qt::AlignBottom);
    v->addLayout(scenario_row);

    for (int i = 0; i < 2; ++i) {
        QComboBox *&box = (i == 0 ? base1_ : base2_);
        box = make_combo();
        connect(box, &QComboBox::currentIndexChanged, this,
                &MainWindow::on_selection_changed);
    }
    v->addLayout(field_row({field("Base", base1_), field("Second base", base2_)}));
    split_ = new QCheckBox("Split the two bases 50:50");
    split_->setChecked(true);
    split_->setToolTip("The menu's ProportionalReductionForPortionTarget rule: halve each "
                       "base, then floor it at per_portion_minimum_weight_g. Off, both "
                       "bases start at their full portion. The real rule only fires for "
                       "the menu items it lists.");
    connect(split_, &QCheckBox::toggled, this, &MainWindow::on_selection_changed);
    v->addWidget(split_);

    // Ingredients as chips; "+ Add" offers only what this brand's menu carries.
    auto picker = [&](const QString &caption, const QString &noun, ChipPicker *&out,
                      QStringList *picked) {
        v->addSpacing(space::xs);
        v->addWidget(section_label(caption));
        out = new ChipPicker(noun);
        out->set_swatches([](const QString &name) { return theme::food_colour(name, 2); });
        connect(out, &ChipPicker::changed, this, [this, picked](const QStringList &now) {
            *picked = now;
            on_selection_changed();
        });
        v->addWidget(out);
    };
    picker("Proteins \u00B7 up to 2", "protein", protein_chips_, &picked_proteins_);
    picker("Toppings", "topping", topping_chips_, &picked_toppings_);
    // Sauces go in cups, never into the bowl: each one picked is a cup that takes
    // room from the food.
    picker(QString("Sauces \u00B7 up to %1 \u00B7 one cup each").arg(SauceCups::kMax),
           "sauce", sauce_chips_, &picked_sauces_);
    sauce_chips_->set_limit(SauceCups::kMax);

    // ---- the dispense model's one recipe-level input ----------------------
    legacy_group_ = titled_panel("Dynamic portioning", v);
    floor_panel_title_ = legacy_group_->findChild<QLabel *>();
    floor_ = make_spin(0, 5000, 5, 0);
    floor_->setToolTip("The weight the bowl is topped up to (minimum_product_weight_g), "
                       "matched from the menu's rules. A value you type stays until you "
                       "change brand or go back to the menu's.");
    // Programmatic updates are signal-blocked, so this only fires for typed values.
    connect(floor_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
        floor_override_ = val;
        apply_floor();
        recompute();
    });
    // Volume Fill goes by volume alone, so the floor is hidden while it runs.
    floor_fields_ = new QWidget;
    floor_fields_->setObjectName("clear");
    auto *ffl = new QVBoxLayout(floor_fields_);
    ffl->setContentsMargins(0, 0, 0, 0);
    ffl->setSpacing(space::md);
    ffl->addWidget(field("Weight floor (g)", floor_));
    auto *floor_row = new QHBoxLayout;
    floor_note_ = make_label("", "note");
    floor_row->addWidget(floor_note_, 1);
    floor_reset_ = new QPushButton("Use the menu's");
    floor_reset_->setObjectName("quiet");
    floor_reset_->setVisible(false);
    connect(floor_reset_, &QPushButton::clicked, this, [this] {
        floor_override_.reset();
        apply_floor();
        recompute();
    });
    floor_row->addWidget(floor_reset_, 0, Qt::AlignTop);
    ffl->addLayout(floor_row);
    v->addWidget(floor_fields_);

    // Volume Fill's two knobs: what counts as fitting, and how far it may step down.
    volume_fields_ = new QWidget;
    volume_fields_->setObjectName("clear");
    auto *vfl = new QVBoxLayout(volume_fields_);
    vfl->setContentsMargins(0, space::xs, 0, 0);
    vfl->setSpacing(space::md);
    {
        QSettings prefs;
        fit_pct_ = make_spin(50, 110, 1, 0);
        fit_pct_->setSuffix(" %");
        fit_pct_->setValue(prefs.value("volume_fill/fit_pct", 100).toDouble());
        fit_pct_->setToolTip("How full the food may get: 100% is the whole food room. Lower "
                             "it to leave headroom when real bowls run fuller than predicted.");
        min_pct_ = make_spin(0, 100, 5, 0);
        min_pct_->setSuffix(" %");
        min_pct_->setValue(prefs.value("volume_fill/min_pct", 70).toDouble());
        min_pct_->setToolTip("When a bowl is over, bases and toppings step down by their "
                             "step_g but never below this share of their recipe portion. "
                             "Proteins are never stepped down.");
        connect(fit_pct_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
            QSettings().setValue("volume_fill/fit_pct", val);
            recompute();
        });
        connect(min_pct_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
            QSettings().setValue("volume_fill/min_pct", val);
            recompute();
        });
    }
    vfl->addLayout(field_row({field("Fit within (food room)", fit_pct_),
                              field("Step down no lower than", min_pct_)}));
    volume_fields_->setVisible(false);
    v->addWidget(volume_fields_);
    col->addWidget(legacy_group_);

    adaptive_group_ = titled_panel("Adaptive fill", v);
    target_fill_ = make_spin(10, 100, 1, 0);
    target_fill_->setValue(85);
    target_fill_->setToolTip("Where the food should land, as a share of the food room.");
    band_low_ = make_spin(0, 100, 1, 0);
    band_low_->setValue(75);
    band_low_->setToolTip("A bowl already between this and the target is dispensed as "
                          "written.");
    v->addLayout(field_row({field("Target fill %", target_fill_),
                            field("Leave alone above %", band_low_)}));
    tol_base_ = make_spin(0, 100, 1, 0);      tol_base_->setValue(20);
    tol_protein_ = make_spin(0, 100, 1, 0);   tol_protein_->setValue(5);
    tol_topping_ = make_spin(0, 100, 1, 0);   tol_topping_->setValue(10);
    for (QDoubleSpinBox *sp : {tol_base_, tol_protein_, tol_topping_})
        sp->setToolTip("How far each kind may move from its recipe portion. The one dial "
                       "moves every ingredient the same share of its own allowance, so "
                       "20:5:10 is the document's \"4:1:2 ratio steps\".");
    v->addLayout(field_row({field("Base ±%", tol_base_), field("Protein ±%", tol_protein_),
                            field("Topping ±%", tol_topping_)}));
    menu_price_ = make_spin(0, 100, 0.25, 2);
    menu_price_->setValue(12.95);
    menu_price_->setPrefix("$");
    cogs_target_ = make_spin(0, 100, 1, 0);
    cogs_target_->setValue(23);
    cogs_target_->setToolTip("Reported against, never solved for.");
    v->addLayout(field_row({field("Menu price", menu_price_),
                            field("COGS target %", cogs_target_)}));
    for (QDoubleSpinBox *sp : {target_fill_, band_low_, tol_base_, tol_protein_,
                               tol_topping_, menu_price_, cogs_target_})
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    adaptive_group_->setVisible(false);
    col->addWidget(adaptive_group_);

    col->addStretch(1);
    scroll->setWidget(host);
    return scroll;
}

QWidget *MainWindow::build_settings()
{
    auto *pane = new QWidget;
    pane->setObjectName("clear");
    auto *outer = new QVBoxLayout(pane);
    outer->setContentsMargins(kEdgeInset, kEdgeInset, kEdgeInset, kEdgeInset);
    outer->setSpacing(0);
    QFrame *panel = make_panel();
    outer->addWidget(panel);
    auto *pv = new QVBoxLayout(panel);
    pv->setContentsMargins(space::xl, space::lg, space::xl, space::lg);
    pv->setSpacing(space::lg);
    auto *head = new QHBoxLayout;
    head->setSpacing(space::lg);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(space::xs);
    titles->addWidget(make_label("Settings", "heading"));
    settings_summary_ = make_label("", "note");
    settings_summary_->setToolTip("The bowl on screen, recomputed as you change settings.");
    titles->addWidget(settings_summary_);
    head->addLayout(titles, 1);
    auto *back = new QPushButton("←  Back to the bowl");
    back->setObjectName("primary");
    connect(back, &QPushButton::clicked, this, [this] { settings_->setChecked(false); });
    head->addWidget(back, 0, Qt::AlignTop);
    pv->addLayout(head);

    auto *body = new QHBoxLayout;
    body->setSpacing(space::xl);
    auto *nav = new QVBoxLayout;
    nav->setSpacing(2);
    auto *pages = new QStackedWidget;
    pages->setObjectName("clear");
    body->addLayout(nav);
    body->addWidget(pages, 1);
    pv->addLayout(body, 1);

    // One page per concern: a nav button, a scrolling body, and a reset for the
    // page's settings. Returns the layout the page's content goes into.
    auto add_page = [&](const QString &title, const QString &blurb,
                        const QString &nav_title = {}) {
        const int index = pages->count();
        auto *button = new QPushButton(
            QString(nav_title.isEmpty() ? title : nav_title).replace("&", "&&"));
        button->setObjectName("nav");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, pages, index] {
            pages->setCurrentIndex(index);
            for (size_t i = 0; i < settings_nav_.size(); ++i)
                settings_nav_[i]->setChecked(static_cast<int>(i) == index);
            QSettings().setValue("ui/settings_page", index);
        });
        nav->addWidget(button);
        settings_nav_.push_back(button);

        auto *scroll = new QScrollArea;
        scroll->setObjectName("drawerPage");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *host = new QWidget;
        host->setObjectName("clear");
        auto *v = new QVBoxLayout(host);
        v->setContentsMargins(0, 0, space::sm, 0);
        // A readable measure: fields stay a comfortable width on a wide screen.
        host->setMaximumWidth(820);
        v->setSpacing(space::md);
        auto *top = new QHBoxLayout;
        top->addWidget(make_label(QString(title).replace("&", "&amp;"), "heading"), 1);
        auto *reset = new QPushButton("Reset");
        reset->setObjectName("quiet");
        reset->setToolTip("Put this page's settings back to their defaults");
        connect(reset, &QPushButton::clicked, this, [this, index] { reset_settings(index); });
        top->addWidget(reset);
        settings_resets_.push_back(reset);
        v->addLayout(top);
        if (!blurb.isEmpty()) v->addWidget(make_label(blurb, "note"));
        scroll->setWidget(host);
        pages->addWidget(scroll);
        return std::pair{v, index};
    };
    nav->addStretch(1);   // re-added below, after the buttons

    // A setting the drawer can mark as changed and put back. `fallback` gives the
    // default at the time it is asked for, so a unit switch cannot make it stale.
    auto track = [this](int page, QWidget *w, std::function<bool()> changed,
                        std::function<void()> restore) {
        settings_tracked_.push_back({page, std::move(changed), std::move(restore)});
        if (auto *sp = qobject_cast<QDoubleSpinBox *>(w))
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { mark_settings(); });
        else if (auto *cb = qobject_cast<QCheckBox *>(w))
            connect(cb, &QCheckBox::toggled, this, [this] { mark_settings(); });
        else if (auto *b = qobject_cast<QPushButton *>(w))
            connect(b, &QPushButton::toggled, this, [this] { mark_settings(); });
    };
    // Compared at the box's own precision: 32 oz shows as 946 ml, not 946.35.
    auto track_spin = [&](int page, QDoubleSpinBox *sp, std::function<double()> fallback) {
        track(page, sp,
              [sp, fallback] {
                  const double scale = std::pow(10.0, sp->decimals());
                  return std::fabs(sp->value() - std::round(fallback() * scale) / scale) > 1e-9;
              },
              [sp, fallback] { sp->setValue(fallback()); });
    };
    auto track_check = [&](int page, QCheckBox *cb, bool fallback) {
        track(page, cb, [cb, fallback] { return cb->isChecked() != fallback; },
              [cb, fallback] { cb->setChecked(fallback); });
    };
    auto track_choice = [&](int page, QPushButton *the_default) {
        track(page, the_default, [the_default] { return !the_default->isChecked(); },
              [the_default] { if (!the_default->isChecked()) the_default->click(); });
    };

    QSettings prefs;
    // Spin boxes that remember themselves under `key`.
    auto dim = [&](const QString &key, double def, double lo, double hi, int dp,
                   const char *suffix) {
        QDoubleSpinBox *sp = make_spin(lo, hi, dp ? 0.5 : 1, dp);
        sp->setSuffix(suffix);
        sp->setValue(prefs.value(key, def).toDouble());
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key](double val) {
            QSettings().setValue(key, val);
            recompute();
        });
        return sp;
    };
    const BowlGeometry measured;

    // ---- Bowl & lid --------------------------------------------------------
    {
        auto [v, page] = add_page("Bowl & lid",
                                  "The real bowl, lid and sauce cups, measured 2026-10-12. "
                                  "The functional fill is the line food is judged against.");
        capacity_ = make_spin(1, 6000, 1, 0);
        capacity_->setValue(32);
        capacity_->setToolTip("How full a bowl is meant to be: 32 oz, a fill line a little "
                              "below the rim of the real bowl. Food is judged against this "
                              "line; the rim is the hard limit.");
        connect(capacity_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
        auto *cap_field = field("Functional fill (fl oz)", capacity_);
        capacity_label_ = cap_field->findChild<QLabel *>();
        v->addWidget(cap_field);
        track_spin(page, capacity_, [] { return units::from_oz(32); });

        heights_ = new QCheckBox("Judge fit by height under the lid");
        heights_->setChecked(prefs.value("geometry/enabled", true).toBool());
        heights_->setToolTip(QString("Rigid sauce cups and chunky pieces need room below the "
                                     "lid, not just their volume. Off: each cup counts as the "
                                     "%1 ml it takes up.").arg(SauceCups{}.cup_ml, 0, 'f', 0));
        v->addWidget(heights_);
        track_check(page, heights_, true);
        cups_pressed_ = make_toggle("Cups pressed in");
        cups_resting_ = make_toggle("Cups resting on top");
        cups_pressed_->setToolTip("The robot pushes each cup down; food flows round it, so a "
                                  "cup costs only what it displaces.");
        cups_resting_->setToolTip("Cups sit on the food as dispensed, so the whole surface "
                                  "must stay a cup height below the lid.");
        const bool pressed = prefs.value("geometry/cups_pressed", true).toBool();
        cups_pressed_->setChecked(pressed);
        cups_resting_->setChecked(!pressed);
        cups_segment_ = segment({cups_pressed_, cups_resting_});
        v->addWidget(cups_segment_);
        track_choice(page, cups_pressed_);
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
        geometry_note_ = make_label("", "note");
        v->addWidget(geometry_note_);

        v->addWidget(section_label("Bowl"));
        depth_ = dim("bowl/depth_mm", measured.depth_mm, 0, 200, 1, " mm");
        depth_->setToolTip("Inside depth, floor to rim (measured: 46 mm; 51 mm outside).");
        bowl_top_d_ = dim("bowl/top_diameter_mm", measured.top_diameter_mm, 0, 400, 1, " mm");
        bowl_top_d_->setToolTip("Inside diameter at the rim (measured: 181 mm).");
        bowl_bottom_d_ = dim("bowl/bottom_diameter_mm", measured.bottom_diameter_mm, 0, 400, 1,
                             " mm");
        bowl_bottom_d_->setToolTip("Inside diameter at the floor (measured: 161 mm).");
        v->addLayout(field_row({field("Depth", depth_), field("Rim diameter", bowl_top_d_),
                                field("Floor diameter", bowl_bottom_d_)}));
        v->addWidget(section_label("Lid"));
        headroom_ = dim("lid/headroom_mm", measured.lid_headroom_mm, 0, 50, 1, " mm");
        headroom_->setToolTip("How far the inside of the lid rises above the rim in its "
                              "centre (measured: about 5 mm).");
        lid_plateau_d_ = dim("lid/plateau_diameter_mm", measured.lid_plateau_diameter_mm, 0,
                             400, 1, " mm");
        lid_plateau_d_->setToolTip("Across the lid's raised centre (measured: about 160 mm).");
        v->addLayout(field_row({field("Rises", headroom_),
                                field("Over a centre of", lid_plateau_d_)}));
        v->addWidget(section_label("Sauce cup"));
        cup_h_ = dim("cups/height_mm", measured.cup_height_mm, 0, 100, 1, " mm");
        cup_h_->setToolTip("Sauce cup height with its lid on (measured: 30 mm).");
        cup_d_ = dim("cups/top_diameter_mm", measured.cup_diameter_mm, 0, 150, 1, " mm");
        cup_d_->setToolTip("Across the lid (measured: 65 mm). The cup tapers to its base, "
                           "so a pressed-in cup displaces less than a straight column.");
        cup_base_d_ = dim("cups/base_diameter_mm", measured.cup_base_diameter_mm, 0, 150, 1,
                          " mm");
        cup_base_d_->setToolTip("Across the base (measured: 45 mm).");
        v->addLayout(field_row({field("Height", cup_h_), field("Lid diameter", cup_d_),
                                field("Base diameter", cup_base_d_)}));
        const std::pair<QDoubleSpinBox *, double> dims[] = {
            {depth_, measured.depth_mm}, {bowl_top_d_, measured.top_diameter_mm},
            {bowl_bottom_d_, measured.bottom_diameter_mm}, {headroom_, measured.lid_headroom_mm},
            {lid_plateau_d_, measured.lid_plateau_diameter_mm}, {cup_h_, measured.cup_height_mm},
            {cup_d_, measured.cup_diameter_mm}, {cup_base_d_, measured.cup_base_diameter_mm}};
        for (const auto &[sp, def] : dims) track_spin(page, sp, [def] { return def; });
        v->addStretch(1);
    }

    // ---- Dispense ---------------------------------------------------------
    {
        auto [v, page] = add_page("Dispense",
                                  "Which measurements the curves come from, and how far "
                                  "the robot misses its targets.");
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
        track_choice(page, method_robot_);
        method_note_ = make_label("", "note");
        v->addWidget(method_note_);

        v->addWidget(section_label("Dispense variation"));
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
            track_spin(page, sp, [fallback] { return fallback * 100; });
            return sp;
        };
        disp_base_ = pct("base_pct", def.base);
        disp_protein_ = pct("protein_pct", def.protein);
        disp_topping_ = pct("topping_pct", def.topping);
        for (QDoubleSpinBox *sp : {disp_base_, disp_protein_, disp_topping_})
            sp->setToolTip("One standard deviation of the robot's miss, as a share of the "
                           "target weight.");
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
        v->addWidget(make_label("Defaults from 90 days of production dispenses (to "
                                "2026-10-12), leaving out spinach and frozen peas, whose "
                                "targets look wrong.", "note"));
        v->addStretch(1);
    }

    // ---- Mounding & chunks --------------------------------------------------
    {
        auto [v, page] = add_page("Mounding & chunks",
                                  "How food piles when it lands, and how far chunky pieces "
                                  "stand proud. Turn mounding on from the finished bowl.");
        mound_slope_ = dim("mound/slope_deg", BowlGeometry::kPlaceholderMoundSlope, 0, 80, 0,
                           "°");
        mound_slope_->setToolTip("How steep a pile's sides are, the food's angle of repose; "
                                 "0° is a level fill. Read it off a side-on triage photo. "
                                 "Ingredients can set their own under Ingredients.");
        chunk_proud_ = dim("geometry/chunk_proud_pct", BowlGeometry::kPlaceholderChunkProud * 100,
                           0, 100, 0, " %");
        chunk_proud_->setToolTip("How much of a chunky piece's height stands above the "
                                 "smeared food around it.");
        v->addLayout(field_row({field("Default mound slope", mound_slope_),
                                field("Chunks stand proud by", chunk_proud_)}));
        track_spin(page, mound_slope_, [] { return BowlGeometry::kPlaceholderMoundSlope; });
        track_spin(page, chunk_proud_, [] { return BowlGeometry::kPlaceholderChunkProud * 100; });
        auto *per = new QPushButton("Per-ingredient slopes and piece heights…");
        connect(per, &QPushButton::clicked, this, &MainWindow::show_mappings);
        v->addWidget(per);
        v->addStretch(1);
    }

    // ---- Compression --------------------------------------------------------
    {
        auto [v, page] = add_page("Compression",
                                  "Whether the bases are squeezed by what is dispensed on "
                                  "top of them.");
        compress_ = new QCheckBox("Compress the base under the load above it");
        compress_->setChecked(true);
        connect(compress_, &QCheckBox::toggled, this, &MainWindow::on_selection_changed);
        v->addWidget(compress_);
        track_check(page, compress_, true);
        load_transfer_ = make_spin(0, kMaxLoadTransfer, 0.05, 2);
        load_transfer_->setValue(1.0);
        load_transfer_->setToolTip("Share of the topping weight that bears on the base. 1 is "
                                   "the weight alone; above 1 is fitted from scanned bowls, "
                                   "where lid, cups and the drop squash more than weight "
                                   "explains.");
        connect(load_transfer_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
        v->addWidget(field("Load transfer", load_transfer_));
        track_spin(page, load_transfer_, [] { return 1.0; });
        compress_note_ = make_label("", "note");
        v->addWidget(compress_note_);
        v->addStretch(1);
    }

    // ---- Measurements -------------------------------------------------------
    {
        auto [v, page] = add_page("Measurements",
                                  "Every reading lives as a file in data/measurements. "
                                  "Edit or delete files there and reload to correct data.");
        auto *row = new QHBoxLayout;
        row->setSpacing(space::sm);
        auto *upload = new QPushButton("Upload curve CSV…");
        upload->setToolTip("One row per measurement: ingredient, g, ml. The file is copied "
                           "into data/measurements and loaded from there from now on.");
        connect(upload, &QPushButton::clicked, this, &MainWindow::on_upload_csv);
        row->addWidget(upload, 1);
        auto *help = new QPushButton("Format");
        help->setToolTip(qs(CurveSet::format_help()));
        connect(help, &QPushButton::clicked, this, &MainWindow::show_csv_help);
        row->addWidget(help);
        v->addLayout(row);

        auto *bowl_row = new QHBoxLayout;
        bowl_row->setSpacing(space::sm);
        auto *bowls = new QPushButton("Upload bowl data…");
        bowls->setToolTip("One row per scanned bowl: grams per ingredient in dispense "
                          "order, then ml. Single-ingredient rows extend the curves; mixed "
                          "rows check the ranges.");
        connect(bowls, &QPushButton::clicked, this, &MainWindow::on_upload_bowl_data);
        bowl_row->addWidget(bowls, 1);
        auto *bowl_help = new QPushButton("Format");
        bowl_help->setToolTip(qs(bowl_data_format_help()));
        connect(bowl_help, &QPushButton::clicked, this, &MainWindow::show_bowl_help);
        bowl_row->addWidget(bowl_help);
        v->addLayout(bowl_row);

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

        // The camera's zero: what an empty bowl reads. Every scanned volume is
        // corrected by it, so small portions are not under-counted by a fixed few ml.
        empty_bowl_ml_ = make_spin(-30, 30, 0.5, 1);
        empty_bowl_ml_->setSuffix(" ml");
        empty_bowl_ml_->setValue(
            prefs.value("measurements/empty_bowl_ml", kDefaultEmptyBowlMl).toDouble());
        empty_bowl_ml_->setToolTip("What volume-vision reads for an empty bowl (usually -4 "
                                   "to -8 ml). Subtracted from every scanned bowl before "
                                   "curves are fitted. Files with ingredient, g, ml rows are "
                                   "taken as given.");
        connect(empty_bowl_ml_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
            QSettings().setValue("measurements/empty_bowl_ml", val);
            const QStringList problems = reload_measurements();
            if (!problems.isEmpty())
                QMessageBox::warning(this, "Some measurement files could not be loaded",
                                     problems.join("\n"));
            recompute();
        });
        v->addWidget(field("Empty-bowl reading (camera zero)", empty_bowl_ml_));
        track_spin(page, empty_bowl_ml_, [] { return kDefaultEmptyBowlMl; });

        // Both only change how the stored readings are read; the files are never
        // edited, so turning one off brings every reading back.
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
        track_check(page, backfill_, false);
        drop_impossible_ = measurement_toggle(
            "Ignore impossible readings", "measurements/drop_impossible",
            "Leave out of the fits any scan reading less than the food's own solid volume "
            "(mass / 1.05 g/ml): a pile cannot take up less room than it is made of. The "
            "test beans are exempt. The files are not changed.");
        v->addWidget(drop_impossible_);
        track_check(page, drop_impossible_, false);
        curve_note_ = make_label("", "note");
        v->addWidget(curve_note_);

        auto *curve_head = new QHBoxLayout;
        curve_head->addWidget(section_label("Measured mass → volume curves"));
        curve_head->addStretch(1);
        curve_filter_ = new CurveFilter;
        curve_head->addWidget(curve_filter_);
        v->addLayout(curve_head);
        curve_chart_ = new CurveChart;
        curve_chart_->setMinimumHeight(300);
        v->addWidget(curve_chart_);
        connect(curve_filter_, &CurveFilter::changed, curve_chart_, &CurveChart::set_hidden);
        v->addStretch(1);
    }

    // ---- Ingredients & menu ---------------------------------------------------
    {
        auto [v, page] = add_page("Ingredients & menu",
                                  "How each brand's ingredient names map onto shared names, "
                                  "and what this brand's menu export carries.");
        settings_resets_[page]->setVisible(false);   // nothing here has a default
        auto *names = new QPushButton("Names, kinds, piece heights and slopes…");
        names->setToolTip("Map each menu's spelling of an ingredient onto a common name, "
                          "so brands that call it different things share its cost and "
                          "curve; correct its kind; give chunky ingredients a piece height "
                          "and any ingredient its own mound slope.");
        connect(names, &QPushButton::clicked, this, &MainWindow::show_mappings);
        v->addWidget(names);
        mapping_note_ = make_label("", "note");
        v->addWidget(mapping_note_);
        update_mapping_note();
        v->addWidget(section_label("This brand's menu"));
        brand_note_ = make_label("", "note");
        v->addWidget(brand_note_);
        v->addStretch(1);
    }

    // ---- Robot-dispensed ingredients ---------------------------------------------
    {
        auto [v, page] = add_page(
            "Robot-dispensed ingredients",
            "What is loaded on the robot, station by station, and the sauces it serves. "
            "These make up the <b>Manual Entry</b> brand, where any of them can go in a "
            "bowl as whatever it is set to serve as. Pick a name any brand uses, or type "
            "one in; a name no menu carries takes typical ramp settings for its kind.",
            "Robot stations");
        settings_resets_[page]->setVisible(false);   // nothing here has a default

        auto *use = new QPushButton("Build bowls with Manual Entry");
        use->setObjectName("primary");
        use->setToolTip("Switch the brand to Manual Entry and go back to the bowl");
        connect(use, &QPushButton::clicked, this, [this] {
            select(qs(kManualEntryBrand));
            open_settings(false);
        });
        auto *top = new QHBoxLayout;
        top->addWidget(use);
        top->addStretch(1);
        v->addLayout(top);

        v->addWidget(section_label("Stations"));
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(space::md);
        grid->setVerticalSpacing(space::xs);
        grid->setColumnStretch(1, 1);
        grid->addWidget(make_label("Station", "note"), 0, 0);
        grid->addWidget(make_label("Ingredient", "note"), 0, 1);
        grid->addWidget(make_label("Serves as", "note"), 0, 2);
        station_rows_.assign(ManualEntry::kStations, {});
        for (int i = 0; i < ManualEntry::kStations; ++i) {
            auto *num = make_label(QString::number(i + 1), "note");
            num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto *name = make_combo();
            name->setEditable(true);
            name->setInsertPolicy(QComboBox::NoInsert);
            name->setMaxVisibleItems(20);
            name->lineEdit()->setPlaceholderText("Empty");
            name->completer()->setCompletionMode(QCompleter::PopupCompletion);
            name->completer()->setFilterMode(Qt::MatchContains);
            name->completer()->setCaseSensitivity(Qt::CaseInsensitive);
            auto *kind = make_combo();
            for (const std::string &k : kMappingKinds) kind->addItem(capitalised(qs(k)), qs(k));
            kind->setFixedWidth(130);
            station_rows_[i] = {name, kind};
            grid->addWidget(num, i + 1, 0);
            grid->addWidget(name, i + 1, 1);
            grid->addWidget(kind, i + 1, 2);

            // A new name takes the kind the brands give it; the kind can then be
            // changed on its own.
            auto commit_name = [this, i] {
                const StationRow &row = station_rows_[i];
                const QString text = row.name->currentText().trimmed();
                ManualStation &st = manual_.stations[i];
                if (ss(text) == st.name) return;
                st.name = ss(text);
                st.kind = text.isEmpty() ? std::string() : catalog_kind(menus_, ss(text));
                const QSignalBlocker block(row.kind);
                row.kind->setCurrentIndex(text.isEmpty() ? -1
                                                         : std::max(0, row.kind->findData(qs(st.kind))));
                row.kind->setEnabled(!text.isEmpty());
                row.name->setToolTip(!text.isEmpty() && !contains(ingredient_catalog(menus_), ss(text))
                                         ? "Written in: no brand carries this name, so it "
                                           "takes typical ramp settings for its kind"
                                         : QString());
                apply_manual_entry();
            };
            connect(name, &QComboBox::activated, this, commit_name);
            connect(name->lineEdit(), &QLineEdit::editingFinished, this, commit_name);
            connect(kind, &QComboBox::activated, this, [this, i, kind] {
                if (manual_.stations[i].name.empty()) return;
                manual_.stations[i].kind = ss(kind->currentData().toString());
                apply_manual_entry();
            });
        }
        v->addLayout(grid);

        auto *clear = new QPushButton("Clear all stations…");
        clear->setObjectName("quiet");
        connect(clear, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "Clear all stations",
                                      "Empty all 19 stations? The sauces and floor stay.")
                != QMessageBox::Yes)
                return;
            manual_.stations.assign(ManualEntry::kStations, {});
            refresh_station_catalog();
            apply_manual_entry();
        });
        auto *clear_row = new QHBoxLayout;
        clear_row->addWidget(clear);
        clear_row->addStretch(1);
        v->addLayout(clear_row);

        v->addWidget(section_label("Sauces"));
        v->addWidget(make_label("Served in cups rather than from a station. A station set "
                                "to serve as a sauce is offered as one too.", "note"));
        manual_sauces_ = new ChipPicker("sauce");
        manual_sauces_->set_write_in(true);
        connect(manual_sauces_, &ChipPicker::changed, this, [this](const QStringList &picked) {
            manual_.sauces = ssl(picked);
            apply_manual_entry();
        });
        v->addWidget(manual_sauces_);

        v->addWidget(section_label("Weight floor"));
        v->addWidget(make_label("The weight every Manual Entry bowl ramps up to under dynamic "
                                "portioning. None leaves bowls as built. Volume fill goes "
                                "by volume alone and ignores it.",
                                "note"));
        auto *floor = make_spin(0, 3000, 10, 0);
        floor->setSuffix(" g");
        floor->setSpecialValueText("None");
        floor->setValue(manual_.floor_g);
        floor->setFixedWidth(130);
        connect(floor, &QDoubleSpinBox::valueChanged, this, [this](double g) {
            manual_.floor_g = g;
            apply_manual_entry();
        });
        v->addWidget(floor);
        refresh_station_catalog();
        v->addStretch(1);
    }

    // ---- Volume per gram --------------------------------------------------------
    // Last in the list, so the saved page index and the estimate links keep pointing at
    // the pages they always have.
    {
        auto [v, page] = add_page(
            "Volume per gram",
            "How much room a gram of each of this brand's ingredients takes, and what its "
            "ramp step adds. A gram of romaine takes several times the room of a gram of "
            "rice, so equal gram steps grow a bowl unevenly; the last column is the step "
            "that would make every ingredient add the same volume. Everything is read at "
            "the ingredient's menu portion, where its ramp starts, with nothing pressing "
            "on it: the curves bend, so a gram is worth a little less further up the ramp.");
        // A wide table: let this page use more of the window than the reading measure.
        v->parentWidget()->setMaximumWidth(1180);
        step_target_ml_ = make_spin(1, 200, 1, 1);
        step_target_ml_->setSuffix(" ml");
        step_target_ml_->setFixedWidth(130);
        step_target_ml_->setValue(prefs.value("steps/target_ml", 10.0).toDouble());
        step_target_ml_->setToolTip("The volume every ingredient's step should add. The last "
                                    "column is the step, in grams, that adds it.");
        connect(step_target_ml_, &QDoubleSpinBox::valueChanged, this, [this](double ml) {
            QSettings().setValue("steps/target_ml", ml);
            update_step_table();
        });
        track_spin(page, step_target_ml_, [] { return 10.0; });

        auto csv = [this] {
            const Menu *m = menu();
            if (!m) return std::string();
            const double target = step_target_ml_->value();
            return step_table_csv(m->brand, step_table(*m, curves_, method(), target), target);
        };
        auto *copy = new QPushButton("Copy CSV");
        copy->setToolTip("Copy this brand's table to the clipboard, for a spreadsheet");
        connect(copy, &QPushButton::clicked, this,
                [csv] { QApplication::clipboard()->setText(qs(csv())); });
        auto *save = new QPushButton("Save CSV…");
        connect(save, &QPushButton::clicked, this, [this, csv] {
            const Menu *m = menu();
            if (!m) return;
            const QString suggested = QString("%1-volume-per-gram.csv")
                                          .arg(qs(m->brand).replace(' ', '-').toLower());
            const QString path = QFileDialog::getSaveFileName(this, "Save volume per gram",
                                                              suggested, "CSV (*.csv)");
            if (path.isEmpty()) return;
            if (!write_text(ss(path), csv()))
                QMessageBox::warning(this, "Could not save", "Could not write " + path);
        });
        auto *row = new QHBoxLayout;
        row->setSpacing(space::sm);
        row->addWidget(field("Volume per step", step_target_ml_));
        row->addStretch(1);
        row->addWidget(copy, 0, Qt::AlignBottom);
        row->addWidget(save, 0, Qt::AlignBottom);
        v->addLayout(row);

        step_table_ = new QTableWidget(0, 10);
        step_table_->setObjectName("breakdown");
        step_table_->setShowGrid(false);
        step_table_->verticalHeader()->setVisible(false);
        step_table_->verticalHeader()->setDefaultSectionSize(32);
        step_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        step_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        step_table_->setSelectionMode(QAbstractItemView::NoSelection);
        step_table_->setFocusPolicy(Qt::NoFocus);
        // The page scrolls, not the table: it is sized to its rows in update_step_table.
        step_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        step_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        step_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        v->addWidget(step_table_);
        step_note_ = make_label("", "note");
        v->addWidget(step_note_);
        v->addStretch(1);
    }

    // The stretch added before the pages existed sits above their buttons; move it
    // below them.
    if (QLayoutItem *first = nav->takeAt(0)) delete first;
    nav->addStretch(1);
    for (QPushButton *b : settings_nav_) b->setFixedWidth(190);

    const int page = std::clamp(prefs.value("ui/settings_page", 0).toInt(), 0,
                                pages->count() - 1);
    pages->setCurrentIndex(page);
    settings_nav_[page]->setChecked(true);
    return pane;
}

void MainWindow::open_settings(bool open)
{
    QSettings().setValue("ui/settings_open", open);
    if (settings_->isChecked() != open) {
        const QSignalBlocker block(settings_);
        settings_->setChecked(open);
    }
    body_->setCurrentIndex(open ? 1 : 0);
}

void MainWindow::mark_settings()
{
    std::vector<bool> changed(settings_nav_.size(), false);
    for (const TrackedSetting &t : settings_tracked_)
        if (t.changed()) changed[t.page] = true;
    for (size_t i = 0; i < settings_nav_.size(); ++i) {
        QPushButton *b = settings_nav_[i];
        if (b->property("modified").toBool() == changed[i]) continue;
        b->setProperty("modified", static_cast<bool>(changed[i]));
        b->setToolTip(changed[i] ? "Changed from the defaults" : QString());
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    for (size_t i = 0; i < settings_resets_.size(); ++i)
        settings_resets_[i]->setEnabled(changed[i]);
}

void MainWindow::reset_settings(int page)
{
    for (const TrackedSetting &t : settings_tracked_)
        if (t.page == page && t.changed()) t.restore();
    mark_settings();
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

    // ---- key numbers, a row along the foot of the verdict --------------------
    auto *stats = new QFrame;
    stats->setObjectName("statRow");
    auto *sh = new QHBoxLayout(stats);
    sh->setContentsMargins(0, space::sm, 0, 0);
    sh->setSpacing(0);
    for (int i = 0; i < kStatTiles; ++i) {
        auto *tile = new QFrame;
        tile->setObjectName(i == 0 ? "statFirst" : "stat");
        auto *tv = new QVBoxLayout(tile);
        tv->setContentsMargins(i == 0 ? 0 : space::lg, 0, space::lg, 0);
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
    stat_key_[kStatTiles - 1]->setText("Bowl COGS");
    vv->addSpacing(space::xs);
    vv->addWidget(stats);

    // ---- the finished bowl and where its volume goes -----------------------
    // One card: the bowl and its reference photos on the left, and on the right the
    // breakdown table, which is also the bowl's legend.
    QVBoxLayout *bv = nullptr;
    v->addWidget(titled_panel("", bv));
    auto *bowl_head = new QHBoxLayout;
    bowl_head->addWidget(make_label("The finished bowl", "heading"), 1);
    // A view of the same bowl, not a setting of the model: it sits on the card.
    mound_ = new QCheckBox("Show mounding");
    mound_->setChecked(QSettings().value("mound/enabled", false).toBool());
    mound_->setToolTip("Food dropped at the centre piles up instead of levelling out, so a "
                       "bowl can stand over the rim with room to spare by volume. Changes "
                       "the diagram and reports the peak; fit is still judged on volume, "
                       "since the lidder presses the mound down. Slopes are in Settings.");
    connect(mound_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("mound/enabled", on);
        recompute();
    });
    bowl_head->addWidget(mound_);
    bowl_head->addSpacing(space::md);
    auto *spec = new QPushButton("Export spec\u2026");
    spec->setToolTip("Save a one-page PDF of this bowl: portions, steps and maxima, the "
                     "predicted fill and its odds, and the estimates it rests on.");
    connect(spec, &QPushButton::clicked, this, &MainWindow::export_spec);
    bowl_head->addWidget(spec);
    bv->addLayout(bowl_head);
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
    // Portions are edited here: click a tinted cell, type, Enter or Tab.
    breakdown_->setEditTriggers(QAbstractItemView::CurrentChanged
                                | QAbstractItemView::SelectedClicked
                                | QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed
                                | QAbstractItemView::AnyKeyPressed);
    breakdown_->setSelectionMode(QAbstractItemView::SingleSelection);
    breakdown_->setFocusPolicy(Qt::StrongFocus);
    breakdown_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    breakdown_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    breakdown_->setItemDelegate(new PortionDelegate(
        [this](int row, int column, double value) { on_portion_edited(row, column, value); },
        breakdown_));
    connect(breakdown_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != breakdown_->columnCount() - 1) return;
        const QTableWidgetItem *reset = breakdown_->item(row, column);
        const QTableWidgetItem *name = breakdown_->item(row, 0);
        if (!reset || reset->text().isEmpty() || !name) return;
        overrides_.erase(name->data(kNameRole).toString());
        QTimer::singleShot(0, this, [this] { recompute(); });
    });
    rv->addWidget(breakdown_);
    auto *table_foot = new QHBoxLayout;
    table_foot->setSpacing(space::md);
    foot_note_ = make_label("", "note");
    table_foot->addWidget(foot_note_, 1);
    reset_edits_ = new QPushButton("Reset all edits");
    reset_edits_->setObjectName("quiet");
    reset_edits_->setToolTip("Your portion edits are kept as you change the bowl, until you "
                             "change brand. This drops them all now.");
    connect(reset_edits_, &QPushButton::clicked, this, [this] {
        overrides_.clear();
        recompute();
    });
    table_foot->addWidget(reset_edits_, 0, Qt::AlignTop);
    rv->addLayout(table_foot);

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
    set_model(adaptive ? Model::Adaptive : Model::Dynamic);
}

void MainWindow::set_model(Model model)
{
    model_ = model;
    adaptive_ = model == Model::Adaptive;
    const bool volume = model == Model::VolumeFill;
    mode_legacy_->setChecked(model == Model::Dynamic);
    mode_adaptive_->setChecked(adaptive_);
    mode_volume_->setChecked(volume);
    // Dynamic portioning ramps to the weight floor; Volume Fill ignores it and has its
    // fit target and step-down limit instead.
    legacy_group_->setVisible(!adaptive_);
    floor_panel_title_->setText(volume ? "Volume fill" : "Dynamic portioning");
    floor_fields_->setVisible(!volume);
    volume_fields_->setVisible(volume);
    adaptive_group_->setVisible(adaptive_);
    for (int i = 0; i < 5; ++i)
        stat_key_[i]->setText(adaptive_ ? kAdaptiveKeys[i] : kLegacyKeys[i]);
    ramp_panel_->setVisible(!adaptive_);
    tol_panel_->setVisible(adaptive_);
    recompute();
}

VolumeFillSettings MainWindow::volume_fill_settings() const
{
    VolumeFillSettings vf;
    vf.fit_share = fit_pct_->value() / 100.0;
    vf.min_share = min_pct_->value() / 100.0;
    return vf;
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
    mark_settings();   // the capacity's default is in the new unit too
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
    const Ingredient *ing = m ? m->find(ss(name)) : nullptr;
    if (!ing) return 0.0;
    // An unpinned base starts at its portion, halved by the split when two are in.
    if (fieldName == "start" && ing->kind == Kind::Base)
        return split_base_start(*ing, split_->isChecked(),
                                !base2_->currentData().toString().isEmpty());
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
    for (const QString &name : bowl_names()) {
        const Ingredient *ing = m->find(ss(name));
        if (!ing) continue;
        BowlItem it = make_item(*ing, curves_, method(), kFlatProteinRate, kFlatToppingRate);
        it.start_g = value_for(name, "start");
        it.step_g = value_for(name, "step");
        it.max_g = value_for(name, "max");
        it.mound_slope_deg = mound_slope_for(qs(it.name));
        if (const auto roll = roll_off_.find(it.name); roll != roll_off_.end())
            it.roll_off_deg = roll->second;
        if (!it.has_curve) {
            it.flat_oz_per_100g = value_for(name, "rate");
        } else if (auto ov = overrides_.find(name);
                   ov != overrides_.end() && ov->second.curve_scale) {
            it.K *= *ov->second.curve_scale;   // same shape, more or less volume
        }
        items.push_back(it);
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

QString MainWindow::rolloff_path() const
{
    return asset_dir_ + "/data/ingredient_rolloff.csv";
}

double MainWindow::mound_slope_for(const QString &name) const
{
    const auto it = mound_slopes_.find(ss(name));
    return it != mound_slopes_.end() ? it->second : -1.0;
}

void MainWindow::update_mapping_note()
{
    if (!mapping_note_) return;
    QSet<QString> brands;
    for (const auto &[key, m] : name_maps_) brands << qs(key.first);
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
    const std::vector<Menu> loaded = loaded_menus();
    MappingDialog dialog(loaded, name_maps_, pieces_, mound_slopes_, roll_off_,
                         brand_->currentData().toString(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const NameMappings next = dialog.mappings();
    const PieceHeights next_pieces = dialog.pieces();
    const MoundSlopes next_slopes = dialog.slopes();
    const RollOffAngles next_rolls = dialog.rolls();
    std::string err;
    if (!save_name_mappings(ss(mappings_path()), next, &err)
        || !save_piece_heights(ss(pieces_path()), next_pieces, &err)
        || !save_mound_slopes(ss(mounds_path()), next_slopes, &err)
        || !save_roll_off(ss(rolloff_path()), next_rolls, &err)) {
        QMessageBox::warning(this, "Could not save ingredients", qs(err));
        return;
    }
    name_maps_ = next;
    pieces_ = next_pieces;
    mound_slopes_ = next_slopes;
    roll_off_ = next_rolls;
    reload_menus();
}

void MainWindow::reload_menus()
{
    // Names may have changed under the current selection, so hold on to the brand and
    // recipe (recipe names come from the menu's template ids, not its ingredients).
    const QString brand = brand_->currentData().toString();
    const QString recipe = recipe_->currentData().toString();

    LoadResult loaded = load_menus(ss(asset_dir_ + "/menus"), &name_maps_, &pieces_);
    menus_ = std::move(loaded.menus);
    menu_issues_ = qsl(loaded.issues);
    rebuild_manual_menu();
    refresh_station_catalog();
    issues_->setVisible(!menu_issues_.isEmpty());
    issues_->setText(QString("%1 menu issue%2")
                         .arg(menu_issues_.size())
                         .arg(menu_issues_.size() == 1 ? "" : "s"));
    issues_->setToolTip(menu_issues_.join("\n"));
    update_mapping_note();

    populate_brand();
    select(brand, recipe);
}

std::vector<Menu> MainWindow::loaded_menus() const
{
    std::vector<Menu> out;
    for (const Menu &m : menus_)
        if (m.brand != kManualEntryBrand) out.push_back(m);
    return out;
}

void MainWindow::rebuild_manual_menu()
{
    std::erase_if(menus_, [](const Menu &m) { return m.brand == kManualEntryBrand; });
    menus_.push_back(manual_entry_menu(menus_, manual_, &pieces_));
}

void MainWindow::apply_manual_entry()
{
    save_manual_entry(manual_);
    const bool showing = brand_ && brand_->currentData().toString() == qs(kManualEntryBrand);
    rebuild_manual_menu();
    // Its ingredients changed under the bowl, so the bowl starts again.
    if (showing) {
        populate_brand();
        select(qs(kManualEntryBrand));
    }
}

void MainWindow::refresh_station_catalog()
{
    if (station_rows_.empty()) return;
    const QStringList catalog = qsl(ingredient_catalog(menus_));
    for (int i = 0; i < static_cast<int>(station_rows_.size()); ++i) {
        const StationRow &row = station_rows_[i];
        const ManualStation &st = manual_.stations[i];
        const QSignalBlocker block_name(row.name), block_kind(row.kind);
        row.name->clear();
        row.name->addItem(QString());
        row.name->addItems(catalog);
        row.name->setCurrentIndex(0);
        row.name->setEditText(qs(st.name));
        row.name->setToolTip(!st.name.empty() && !catalog.contains(qs(st.name))
                                 ? "Written in: no brand carries this name, so it takes "
                                   "typical ramp settings for its kind"
                                 : QString());
        // An empty station shows no kind until it holds something.
        row.kind->setCurrentIndex(st.name.empty() ? -1
                                                  : std::max(0, row.kind->findData(qs(st.kind))));
        row.kind->setEnabled(!st.name.empty());
    }
    if (manual_sauces_) {
        QStringList options = qsl(sauce_catalog(menus_));
        for (const QString &n : qsl(manual_.sauces))
            if (!options.contains(n)) options << n;
        const QSignalBlocker block(manual_sauces_);
        manual_sauces_->set_options(options);
        manual_sauces_->set_picked(qsl(manual_.sauces));
    }
}

void MainWindow::populate_brand()
{
    loading_ = true;
    brand_->clear();
    for (const Menu &m : menus_) brand_->addItem(qs(m.brand), qs(m.brand));
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
        for (const QString &n : qsl(m->names_of(Kind::Base))) box->addItem(n, n);
    }
    const QStringList bases = qsl(m->names_of(Kind::Base));
    if (!bases.isEmpty()) base1_->setCurrentIndex(0);
    base2_->setCurrentIndex(0);

    recipe_->clear();
    recipe_->addItem("— build your own —", QString());
    // Manual Entry's one recipe is the build-your-own shell the report sweeps.
    const bool manual = m->brand == kManualEntryBrand;
    for (const Recipe &r : m->recipes)
        if (!manual || !r.customer_built) recipe_->addItem(qs(r.name), qs(r.name));
    recipe_->setCurrentIndex(0);

    const QStringList proteins = qsl(m->names_of(Kind::Protein));
    if (!proteins.isEmpty()) picked_proteins_ << proteins.front();

    brand_note_->setText(
        manual
            ? QString("Built from the robot's stations under <b>Robot-dispensed "
                      "ingredients</b>: <b>%1</b> dispensed ingredient%2 and <b>%3</b> "
                      "sauce%4. %5")
                  .arg(m->ingredients.size()).arg(m->ingredients.size() == 1 ? "" : "s")
                  .arg(m->sauces.size()).arg(m->sauces.size() == 1 ? "" : "s")
                  .arg(m->floors.empty()
                           ? QString("No weight floor, so nothing ramps.")
                           : QString("Every bowl ramps to a <b>%1 g</b> floor.")
                                 .arg(m->floors.front().minimum_product_weight_g, 0, 'f', 0))
        : m->floors.empty()
            ? QString("<span style='color:%1'>Menu <code>%2</code> has no "
                      "<code>dynamic_portion_increases</code> and no product rules at "
                      "all.</span> Nothing ramps and no base is ever split on this "
                      "brand.")
                  .arg(theme::palette().over.name(), qs(m->version))
            : QString("Menu <code>%1</code> — <b>%2</b> ingredients carry ramp "
                      "settings, <b>%3</b> weight floors. Base-split rule %4.")
                  .arg(qs(m->version))
                  .arg(m->ingredients.size())
                  .arg(m->floors.size())
                  .arg(m->has_split_rule
                           ? QString("covers <b>%1</b> menu items").arg(m->split_rule_items)
                           : QString("is absent")));

    loading_ = false;
    rebuild_ingredient_pickers();
    reload_scenarios();
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
        if (qs(r.name) == want) rec = &r;

    if (!rec) {
        const QStringList bases = qsl(m->names_of(Kind::Base));
        base1_->setCurrentIndex(bases.isEmpty() ? -1 : 0);
        base2_->setCurrentIndex(0);
        const QStringList proteins = qsl(m->names_of(Kind::Protein));
        if (!proteins.isEmpty()) picked_proteins_ << proteins.front();
        // Every bowl comes with one sauce; the second is an upcharge.
        if (!m->sauces.empty()) picked_sauces_ << qs(m->sauces.front());
        recipe_note_->setText(
            m->brand == kManualEntryBrand
                ? (m->ingredients.empty() && m->sauces.empty()
                       ? QString("<span style='color:%1'>No robot stations are set yet.</span> "
                                 "Fill them in under Settings → Robot-dispensed "
                                 "ingredients.").arg(theme::palette().over.name())
                       : QString("Build a bowl from anything loaded on the robot."))
            : m->recipes.empty()
                ? QString("<span style='color:%1'>This menu defines no preconfigured "
                          "bowls.</span>").arg(theme::palette().over.name())
                : "Build your own: pick the ingredients as a customer would.");
    } else {
        QStringList rec_bases;
        for (const RecipeItem &ri : rec->items) {
            const Ingredient *ing = m->find(ri.name);
            if (!ing) continue;
            pins_[qs(ri.name)] = ri.grams;
            if (ing->kind == Kind::Base) rec_bases << qs(ri.name);
            else if (ing->kind == Kind::Protein) picked_proteins_ << qs(ri.name);
            else picked_toppings_ << qs(ri.name);
        }
        auto select = [](QComboBox *box, const QString &name) {
            const int i = box->findData(name);
            box->setCurrentIndex(i >= 0 ? i : 0);
        };
        select(base1_, rec_bases.value(0));
        select(base2_, rec_bases.value(1));
        picked_sauces_ = qsl(m->sauces_in(*rec)).mid(0, SauceCups::kMax);

        const int n = static_cast<int>(rec->items.size())
                      - static_cast<int>(m->sauces_in(*rec).size());
        QString note = QString("Pins <b>%1</b> ingredient%2")
                           .arg(n)
                           .arg(n == 1 ? "" : "s");
        if (!picked_sauces_.isEmpty())
            note += QString(" and <b>%1</b> sauce cup%2 (%3)")
                        .arg(picked_sauces_.size())
                        .arg(picked_sauces_.size() == 1 ? "" : "s")
                        .arg(picked_sauces_.join(", "));
        note += n <= 2 ? ". A template: only the protein is fixed." : ".";
        if (rec->unweighted() > 0)
            note += QString(" <span style='color:%1'>%2 had no weight in the menu, so take "
                            "their normal portion.</span>")
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
    protein_chips_->set_options(qsl(m->names_of(Kind::Protein)));
    topping_chips_->set_options(qsl(m->names_of(Kind::Topping)));
    sauce_chips_->set_options(qsl(m->sauces));
}

int MainWindow::sauce_cup_count() const
{
    return std::min(static_cast<int>(picked_sauces_.size()), SauceCups::kMax);
}

MainWindow::RateInfo MainWindow::rate_info(const QString &name) const
{
    RateInfo info;
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(ss(name)) : nullptr;
    if (!ing) return info;
    const BowlItem item = make_item(*ing, curves_, method());
    const auto ov = overrides_.find(name);
    const Override edit = ov != overrides_.end() ? ov->second : Override{};
    auto shown = [](double oz) {
        return QString("%1 %2/100 g").arg(units::from_oz(oz), 0, 'f', 1).arg(units::suffix());
    };
    if (item.has_curve) {
        // The average over the portion, which is what the portion costs the bowl.
        const double g = value_for(name, "start") > 0 ? value_for(name, "start") : 100.0;
        const double curve = item.volume_oz(g) / g * 100.0;
        const double scale = edit.curve_scale.value_or(1.0);
        info.oz_per_100g = curve * scale;
        info.edited = edit.curve_scale.has_value();
        info.tip = QString("Volume per 100 g at %1 g, from the %2. ").arg(g, 0, 'f', 0)
                       .arg(qs(describe(choose_curve(ss(name), ing->kind, curves_, method()))));
        info.tip += info.edited
                        ? QString("Your edit scales the curve by \u00D7%1 (it gives %2 alone).")
                              .arg(scale, 0, 'f', 2).arg(shown(curve))
                        : QString("Edit it to scale the whole curve; its shape (how it "
                                  "packs as the portion grows) is kept.");
    } else {
        info.oz_per_100g = value_for(name, "rate");
        info.edited = edit.rate.has_value();
        info.tip = "Nothing measured to lean on, so a flat rate is used.";
    }
    return info;
}

void MainWindow::set_rate(const QString &name, double oz_per_100g)
{
    const Menu *m = menu();
    const Ingredient *ing = m ? m->find(ss(name)) : nullptr;
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
}

void MainWindow::on_portion_edited(int row, int column, double value)
{
    const QTableWidgetItem *name = breakdown_->item(row, 0);
    const QTableWidgetItem *cell = breakdown_->item(row, column);
    if (!name || !cell) return;
    const QString ingredient = name->data(kNameRole).toString();
    Override &o = overrides_[ingredient];
    switch (static_cast<Field>(cell->data(kFieldRole).toInt())) {
    case Field::Start: o.start_g = value; break;
    case Field::Step: o.step_g = value; break;
    case Field::Max: o.max_g = value; break;
    case Field::Rate: set_rate(ingredient, units::to_oz(value)); break;
    case Field::None: return;
    }
    // Rebuilding the table from inside its own commit would pull the editor out from
    // under it; let the commit finish first.
    QTimer::singleShot(0, this, [this] { recompute(); });
}

/// The editable portion cells shared by both models' tables.
TableCell MainWindow::portion_cell(const QString &name, Field field, double grams) const
{
    const auto ov = overrides_.find(name);
    const Override edit = ov != overrides_.end() ? ov->second : Override{};
    TableCell c;
    c.field = field;
    c.value = grams;
    c.text = field == Field::Step ? QString::number(grams, 'f', 2) : QString::number(std::round(grams));
    c.edited = (field == Field::Start && edit.start_g) || (field == Field::Step && edit.step_g)
               || (field == Field::Max && edit.max_g);
    return c;
}

TableCell MainWindow::rate_cell(const QString &name) const
{
    const RateInfo info = rate_info(name);
    TableCell c;
    c.field = Field::Rate;
    c.value = units::from_oz(info.oz_per_100g);
    c.text = QString::number(c.value, 'f', 1);
    c.edited = info.edited;
    return c;
}

bool MainWindow::row_edited(const QString &name) const
{
    const auto ov = overrides_.find(name);
    if (ov == overrides_.end()) return false;
    const Override &o = ov->second;
    return o.start_g || o.step_g || o.max_g || o.rate || o.curve_scale;
}

/// The name cell's tooltip: what kind of ingredient, where its curve comes from, and
/// how the portion compares with what was measured.
QString MainWindow::ingredient_tip(const BowlItem &it) const
{
    QString tip = QString("%1 \u00B7 curve: %2")
                      .arg(capitalised(qs(to_string(it.kind))),
                           qs(describe(choose_curve(it.name, it.kind, curves_, method()))));
    const std::string proxy = proxy_curve_for(it.name, curves_);
    if (const Fit *f = curves_.fit(proxy.empty() ? it.name : proxy, method())) {
        tip += QString("\nFit: %1 = %2\u00B7g^%3, R\u00B2 %4, n=%5, measured %6\u2013%7 g")
                   .arg(units::suffix()).arg(fmt(units::scale_k(f->K), 3)).arg(fmt(f->p, 3))
                   .arg(fmt(f->r2, 2)).arg(f->n).arg(std::round(f->lo_g)).arg(std::round(f->hi_g));
        if (it.start_g < f->lo_g * 0.8 || it.max_g > f->hi_g * 1.25)
            tip += "\nThe portion runs outside the measured range.";
    }
    tip += QString("\n%1/100 g: %2").arg(units::suffix()).arg(rate_info(qs(it.name)).tip);
    if (const Menu *m = menu())
        if (const Ingredient *ing = m->find(it.name))
            tip += QString("\nPortion source: %1").arg(qs(to_string(ing->source)));
    return tip;
}

void MainWindow::apply_floor()
{
    const Menu *m = menu();
    if (!m) return;
    const FloorRule *f = m->floor_for(ssl(bowl_names()));
    const double menu_floor = f ? f->minimum_product_weight_g : 0.0;

    const QSignalBlocker block(floor_);
    floor_->setValue(floor_override_.value_or(menu_floor));
    floor_reset_->setVisible(floor_override_.has_value());
    // One line here; the rule that matched is in the tooltip.
    floor_note_->setToolTip({});
    if (floor_override_) {
        floor_note_->setText(QString("Your value. The menu gives %1.")
                                 .arg(f ? QString("%1 g").arg(menu_floor, 0, 'f', 0)
                                        : QString("no floor")));
        return;
    }
    if (m->floors.empty()) {
        floor_note_->setText("This menu sets no floors, so nothing is topped up.");
    } else if (!f) {
        floor_note_->setText(QString("<span style='color:%1'>No floor rule matches this "
                                     "bowl</span>, so nothing is topped up.")
                                 .arg(theme::palette().over.name()));
    } else {
        floor_note_->setText("From the menu's rule for this bowl.");
        QString tip = QString("Matched %1 on %2")
                          .arg(f->op == FilterOp::ExactlyOne ? "ExactlyOne" : "Any",
                               f->must_have.empty() ? QString("anything")
                                                    : qs(join(f->must_have, ", ")));
        if (!f->must_not_have.empty())
            tip += QString(", excluding %1").arg(qs(join(f->must_not_have, ", ")));
        tip += QString(" \u2192 %1 g. The first matching rule wins; type a value to "
                       "override it.").arg(f->minimum_product_weight_g);
        floor_note_->setToolTip(tip);
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
    protein_chips_->set_limit(std::max(2, static_cast<int>(picked_proteins_.size())));
    protein_chips_->set_picked(picked_proteins_);
    topping_chips_->set_picked(picked_toppings_);
    sauce_chips_->set_picked(picked_sauces_);

    // The split only means something with two bases in the bowl.
    split_->setVisible(!base2_->currentData().toString().isEmpty());

    compress_note_->setText(
        compress_->isChecked()
            ? QString("Each base is squeezed by whatever is dispensed after it, by the "
                      "same exponent its curve was fitted with. <span style='color:%1'>No "
                      "loaded bowls were measured, so this is an extrapolation.</span>")
                  .arg(theme::palette().over.name())
            : "Off: volumes are as measured, one ingredient with nothing on top.");
    load_transfer_->setVisible(compress_->isChecked());

    const QString ms = qs(to_string(method()));
    method_note_->setText(
        ms == "robot" ? "Bowls the <b>robot</b> dispensed — the operative case, and the "
                        "most compacted."
        : ms == "hand" ? "<b>Hand</b>-filled reference bowls. Looser packing: kale reads "
                         "+13% at equal mass (p = 0.001), romaine +6%, rice unchanged."
                       : "Both methods <b>pooled</b>. Avoid for kale — its two arms "
                         "differ significantly, so pooling averages two relationships.");

    loading_ = false;
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
                "and the dispense variation in Settings, added as independent.")
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

    // Guesses behind these numbers are listed under the header's estimates button.
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

void MainWindow::update_step_table()
{
    const Menu *m = menu();
    if (!step_table_ || !m) return;
    const double target = step_target_ml_->value();
    const std::vector<StepRow> rows = step_table(*m, curves_, method(), target);

    step_table_->setHorizontalHeaderLabels(
        {"Ingredient", "Kind", "Curve", "Portion", "ml per g", "g per ml", "Next g adds",
         "Step now", "Step adds", QString("Step for %1 ml").arg(target, 0, 'g', 4)});
    const QStringList tips = {
        "",
        "",
        "Where the mass-to-volume curve comes from. Measured is this ingredient's own "
        "readings; borrowed is a relative's; composite averages every measured curve of "
        "its kind; flat is a fixed rate with nothing measured behind it.",
        "The menu portion, where the ramp starts",
        "Volume of the portion over its mass",
        "Mass per ml of the portion: the inverse of ml per g",
        "What one more gram adds at the portion. Below ml per g when the ingredient "
        "compacts, so each gram buys less room than the average one before it.",
        "step_g as the menu sets it",
        "What one step adds from the portion",
        "The step, in grams, that adds the target volume from the portion"};
    for (int c = 0; c < step_table_->columnCount(); ++c)
        if (QTableWidgetItem *h = step_table_->horizontalHeaderItem(c)) h->setToolTip(tips[c]);

    const theme::Palette &pal = theme::palette();
    auto cell = [](const QString &text, bool number) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment((number ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
        return item;
    };
    auto grams = [](double g) { return QString("%1 g").arg(g, 0, 'f', 1); };
    auto ml = [](double v) { return QString("%1 ml").arg(v, 0, 'f', v < 10 ? 2 : 1); };
    int measured = 0;
    step_table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const StepRow &r = rows[i];
        const bool portion = r.portion_g > 0;
        measured += r.source == CurveSource::Own;
        auto *name = cell(qs(r.ingredient), false);
        step_table_->setItem(i, 0, name);
        step_table_->setItem(i, 1, cell(qs(to_string(r.kind)), false));
        auto *curve = cell(qs(curve_label(r.source)), false);
        curve->setToolTip(qs(describe(choose_curve(r.ingredient, r.kind, curves_, method()))));
        // An estimate is shown as one, like the estimates on the bowl.
        if (r.source != CurveSource::Own) curve->setForeground(pal.ink_soft);
        step_table_->setItem(i, 2, curve);
        step_table_->setItem(i, 3, cell(portion ? grams(r.portion_g) : "—", true));
        step_table_->setItem(i, 4, cell(portion ? QString::number(r.ml_per_g, 'f', 2) : "—", true));
        step_table_->setItem(i, 5, cell(portion ? QString::number(r.g_per_ml, 'f', 2) : "—", true));
        step_table_->setItem(i, 6, cell(ml(r.marginal_ml_per_g), true));
        step_table_->setItem(i, 7, cell(r.step_g > 0 ? grams(r.step_g) : "—", true));
        step_table_->setItem(i, 8, cell(r.step_g > 0 ? ml(r.step_ml) : "—", true));
        auto *suggested = cell(QString("%1 g").arg(r.step_g_for_target, 0, 'f', 2), true);
        QFont bold = suggested->font();
        bold.setBold(true);
        suggested->setFont(bold);
        step_table_->setItem(i, 9, suggested);
    }
    const int header = step_table_->horizontalHeader()->sizeHint().height();
    step_table_->setFixedHeight(header + step_table_->verticalHeader()->defaultSectionSize()
                                             * static_cast<int>(rows.size())
                                + 2 * step_table_->frameWidth());

    const QString method_name = method() == Method::Hand     ? "hand-filled"
                                : method() == Method::Pooled ? "pooled"
                                                             : "robot-dispensed";
    step_note_->setText(
        rows.empty()
            ? QString("%1 dispenses nothing from a station.").arg(qs(m->brand))
            : QString("<b>%1</b>, on the %2 curves. <b>%3</b> of %4 ingredients have curves "
                      "of their own; the rest are estimates, and their numbers move as "
                      "measurements come in. Sauces go in cups and have no step.")
                  .arg(qs(m->brand), method_name)
                  .arg(measured)
                  .arg(rows.size()));
}

void MainWindow::recompute()
{
    if (loading_) return;
    const Menu *m = menu();
    if (!m) return;
    update_step_table();

    if (adaptive_) {
        last_adaptive_ = solve_adaptive(assemble_bowl(), adaptive_settings(), &costs_);
        render_adaptive();
        return;
    }

    const bool volume = model_ == Model::VolumeFill;
    VolumeFillResult vfr;
    if (volume) {
        vfr = volume_fill(assemble_bowl(), legacy_settings(), volume_fill_settings());
        last_ = vfr.sim;
    } else {
        last_ = simulate(assemble_bowl(), legacy_settings());
    }
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

    if (volume) {
        // Volume Fill's own outcomes, against its fit target.
        const QString limit = units::volume(vfr.limit_oz, true);
        const QString pct = QString("%1%").arg(std::round(fit_pct_->value()));
        switch (vfr.status) {
        case VolumeFillStatus::FilledToLimit:
            head = vfr.passes_up > 0 ? "Topped up to the limit, and it fits"
                                     : "Fits as written — one more pass would not";
            sub = (vfr.passes_up > 0
                       ? QString("%1 pass%2 up bring it to %3 at %4 g; the next would pass "
                                 "%5 (%6 of the food room). The weight floor plays no part.")
                             .arg(vfr.passes_up).arg(vfr.passes_up == 1 ? "" : "es")
                             .arg(units::volume(last.ounces, true)).arg(std::round(last.grams))
                       : QString("%1 at %2 g; a pass up would take it past %3 (%4 of the "
                                 "food room).")
                             .arg(units::volume(last.ounces, true)).arg(std::round(last.grams)))
                      .arg(limit, pct);
            accent_border = pal.accent; accent_fill = pal.accent_soft; head_colour = pal.accent;
            break;
        case VolumeFillStatus::AtMax:
            head = "Everything at its max, under the limit";
            sub = QString("Every ingredient that steps is at its max after %1 pass%2: %3 at "
                          "%4 g, %5 short of %6.")
                      .arg(vfr.passes_up).arg(vfr.passes_up == 1 ? "" : "es")
                      .arg(units::volume(last.ounces, true)).arg(std::round(last.grams))
                      .arg(units::volume(vfr.limit_oz - last.ounces, true), limit);
            break;
        case VolumeFillStatus::SteppedDown:
            head = "Over as written — stepped down to fit";
            sub = QString("As written it is %1. %2 pass%3 down bring the bases and toppings "
                          "to %4 at %5 g, within %6; proteins keep their portion.")
                      .arg(units::volume(r.first().ounces, true))
                      .arg(vfr.passes_down).arg(vfr.passes_down == 1 ? "" : "es")
                      .arg(units::volume(last.ounces, true)).arg(std::round(last.grams))
                      .arg(limit);
            accent_border = pal.accent; accent_fill = pal.accent_soft; head_colour = pal.accent;
            break;
        case VolumeFillStatus::StillOver:
            head = "Still over, even stepped down as far as allowed";
            sub = QString("With every base and topping at %1% of its portion the bowl is "
                          "%2, past %3. The proteins alone, or the recipe, have to change.")
                      .arg(std::round(min_pct_->value()))
                      .arg(units::volume(last.ounces, true), limit);
            accent_border = pal.over; accent_fill = pal.over_soft; head_colour = pal.over;
            break;
        }
    } else switch (r.verdict) {
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
    if (volume) {
        static const char *const kVolumeKeys[5] = {"Bowl at", "Final volume", "Fill",
                                                   "Passes", "Fit target"};
        for (int i = 0; i < 5; ++i) stat_key_[i]->setText(kVolumeKeys[i]);
        stat_tile_[3]->setVisible(true);
        stat_value_[3]->setText(vfr.passes_down > 0 ? QString("%1 down").arg(vfr.passes_down)
                                                    : QString("%1 up").arg(vfr.passes_up));
        stat_value_[4]->setText(units::volume(vfr.limit_oz, true));
    }
    show_bowl_cost(r.items);

    // ---- breakdown --------------------------------------------------------
    double total_oz = 0;
    for (double oz : last.per_item_oz) total_oz += oz;
    if (total_oz <= 0) total_oz = 1;

    // The diagram goes first: the table is its legend, keyed by the colours it drew.
    diagram_->set_result(r);
    const std::vector<QColor> band = diagram_->item_colours();

    // Start, Step, Max and the rate are edited in place; the rest read the result.
    std::vector<BreakdownRow> rows;
    for (size_t i = 0; i < r.items.size(); ++i) {
        const BowlItem &it = r.items[i];
        const double added_g = it.final_g - it.start_g;
        const double added_oz = last.per_item_oz[i] - r.first().per_item_oz[i];
        const double squeeze =
            last.per_item_oz_uncompressed[i] - last.per_item_oz[i];

        BreakdownRow row;
        row.key = qs(it.name);
        row.name = qs(it.name);
        row.kind = ingredient_tip(it);
        if (settings.compress && squeeze > 0.05)
            row.kind += QString("\nSqueezed %1 by the load above it.")
                            .arg(units::volume(squeeze, true));
        row.swatch = band[i];
        row.edited = row_edited(qs(it.name));
        auto plain = [](const QString &text) { TableCell c; c.text = text; return c; };
        row.cells = {
            portion_cell(qs(it.name), Field::Start, it.start_g),
            portion_cell(qs(it.name), Field::Step, it.step_g),
            portion_cell(qs(it.name), Field::Max, it.max_g),
            plain(QString("%1 g").arg(std::round(it.final_g))),
            plain(std::fabs(added_g) > 0.05
                      ? QString("%1%2").arg(added_g > 0 ? "+" : "\u2212")
                            // Under the display's last digit, say so rather than "0".
                            .arg(units::from_oz(std::fabs(added_oz)) < (units::metric() ? 0.5 : 0.05)
                                     ? QString("<%1 %2").arg(units::metric() ? "1" : "0.1",
                                                             units::suffix())
                                     : units::volume(std::fabs(added_oz), true))
                      : QString("\u2014")),
            plain(units::volume(last.per_item_oz[i], true)),
            plain(QString("%1%").arg(std::round(last.per_item_oz[i] / total_oz * 100))),
            rate_cell(qs(it.name)),
        };
        rows.push_back(row);
    }
    fill_portion_table(rows, {"Ingredient", "Start g", "Step g", "Max g", "Final",
                              "Added", "Volume", "Share",
                              QString("%1/100 g").arg(units::suffix())},
                       {{1, "The portion before any ramp. Click to edit."},
                        {2, "Grams added to this ingredient on each ramp pass. Click to edit."},
                        {3, "The most the dispenser will give. Click to edit."},
                        {5, "Volume the ramp added"},
                        {8, QString("Volume per 100 g at the start portion. Edit it to make "
                                    "the ingredient fluffier or denser; its curve keeps its "
                                    "shape.")}});
    static_cast<BowlCard *>(bowl_row_)->relayout();

    // ---- footnote ---------------------------------------------------------
    std::vector<const BowlItem *> ramped;
    for (size_t i = 0; i < r.items.size(); ++i)
        if (r.items[i].final_g - r.items[i].start_g > 0.05) ramped.push_back(&r.items[i]);

    const QString scaling =
        QString("<b>%1</b> ingredient%2 ramp together at <b>%3 g</b> per pass, of which "
                "the base%4 <b>%5%</b>. %6")
            .arg(r.items.size())
            .arg(r.items.size() == 1 ? "" : "s")
            .arg(fmt(r.grams_per_pass()))
            .arg(std::count_if(r.items.begin(), r.items.end(),
                               [](const BowlItem &i) { return i.kind == Kind::Base; }) == 1
                     ? " supplies" : "s supply")
            .arg(std::round(r.base_share_of_step() * 100))
            .arg(volume ? QString("Every pass is checked against the fit target before it "
                                  "is taken.")
                        : QString("Add another topping and the floor arrives in fewer "
                                  "passes, so the greens are asked for less of it."));

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
                       .arg(qs(worst->name)).arg(fmt(ratio, 2)).arg(qs(best->name))
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
                                .arg(qs(ramped[0]->name))
                                .arg(units::volume(
                                    last.per_item_oz[0] - r.first().per_item_oz[0], true))
                                .arg(std::round(ramped[0]->final_g - ramped[0]->start_g))
                                .arg(r.passes));
    } else {
        foot_note_->setText("Nothing ramps — the bowl already clears its floor, this "
                            "menu sets no floor, or everything starts at its max.");
    }
    if (volume && vfr.passes_up == 0 && vfr.passes_down == 0)
        foot_note_->setText(vfr.status == VolumeFillStatus::AtMax
                                ? QString("Nothing steps: every ingredient is at its max or "
                                          "has no step_g.")
                                : QString("Dispensed as written: one more pass up would not "
                                          "fit."));
    if (volume && vfr.passes_down > 0) {
        // What stepping down took, ingredient by ingredient.
        QStringList taken;
        for (const BowlItem &it : r.items)
            if (it.start_g - it.final_g > 0.05)
                taken << QString("%1 \u2212%2 g").arg(qs(it.name))
                             .arg(QString::number(it.start_g - it.final_g, 'g', 3));
        foot_note_->setText(QString("Stepped down %1 pass%2 by each ingredient's step_g: %3. "
                                    "Proteins are never stepped down; nothing goes below %4% "
                                    "of its portion.")
                                .arg(vfr.passes_down).arg(vfr.passes_down == 1 ? "" : "es")
                                .arg(taken.isEmpty() ? QString("nothing") : taken.join(", "))
                                .arg(std::round(min_pct_->value())));
    }

    render_photos(r.items);

    ramp_chart_->set_show_uncompressed(settings.compress);
    ramp_chart_->set_result(r);
    curve_filter_->set_options(qsl(curves_.ingredients()));
    curve_chart_->set_curves(&curves_, method());

    update_curve_note();
    update_settings_summary(r.fill);
    update_estimates(r.items, nullptr);
}

QString MainWindow::scenarios_dir() const
{
    return asset_dir_ + "/data/scenarios";
}

namespace {

QJsonObject override_json(const Override &o)
{
    QJsonObject j;
    if (o.start_g) j["start_g"] = *o.start_g;
    if (o.step_g) j["step_g"] = *o.step_g;
    if (o.max_g) j["max_g"] = *o.max_g;
    if (o.rate) j["rate_oz_per_100g"] = *o.rate;
    if (o.curve_scale) j["curve_scale"] = *o.curve_scale;
    return j;
}

Override override_from(const QJsonObject &j)
{
    Override o;
    auto read = [&](const char *key, std::optional<double> &out) {
        if (j.contains(key) && j[key].isDouble()) out = j[key].toDouble();
    };
    read("start_g", o.start_g);
    read("step_g", o.step_g);
    read("max_g", o.max_g);
    read("rate_oz_per_100g", o.rate);
    read("curve_scale", o.curve_scale);
    return o;
}

QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray()) out << x.toString();
    return out;
}

}  // namespace

void MainWindow::reload_scenarios(const QString &select)
{
    scenarios_.clear();
    const Menu *m = menu();
    QDir dir(scenarios_dir());
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
        if (!m || j["brand"].toString() != qs(m->brand)) continue;
        Scenario s;
        s.file = f.fileName();
        s.name = j["name"].toString(QFileInfo(name).completeBaseName());
        s.brand = j["brand"].toString();
        s.recipe = j["recipe"].toString();
        s.adaptive = j["mode"].toString() == "adaptive";
        s.volume = j["mode"].toString() == "volume";
        const QStringList bases = strings(j["bases"]);
        s.base1 = bases.value(0);
        s.base2 = bases.value(1);
        s.proteins = strings(j["proteins"]);
        s.toppings = strings(j["toppings"]);
        s.sauces = strings(j["sauces"]);
        s.split = j["split_bases"].toBool(true);
        if (j["floor_g"].isDouble()) s.floor_g = j["floor_g"].toDouble();
        const QJsonObject edits = j["edits"].toObject();
        for (auto it = edits.begin(); it != edits.end(); ++it)
            s.overrides[it.key()] = override_from(it.value().toObject());
        scenarios_.push_back(std::move(s));
    }
    std::sort(scenarios_.begin(), scenarios_.end(), [](const Scenario &a, const Scenario &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    const QSignalBlocker block(scenario_);
    scenario_->clear();
    scenario_->addItem(scenarios_.empty() ? "— none saved —" : "— choose —");
    int pick = 0;
    for (size_t i = 0; i < scenarios_.size(); ++i) {
        scenario_->addItem(scenarios_[i].name);
        if (scenarios_[i].name == select) pick = static_cast<int>(i) + 1;
    }
    scenario_->setCurrentIndex(pick);
    scenario_delete_->setEnabled(pick > 0);
}

void MainWindow::save_scenario()
{
    const Menu *m = menu();
    if (!m) return;
    const QString suggestion = scenario_->currentIndex() > 0 ? scenario_->currentText()
                               : recipe_->currentIndex() > 0 ? recipe_->currentText()
                                                             : QString("My bowl");
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save scenario",
                                               "Name this set-up of the bowl:",
                                               QLineEdit::Normal, suggestion, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QJsonObject j;
    j["name"] = name;
    j["brand"] = qs(m->brand);
    j["recipe"] = recipe_->currentData().toString();
    j["mode"] = adaptive_ ? "adaptive" : model_ == Model::VolumeFill ? "volume" : "dynamic";
    QJsonArray bases;
    for (QComboBox *b : {base1_, base2_})
        if (!b->currentData().toString().isEmpty()) bases.append(b->currentData().toString());
    j["bases"] = bases;
    j["proteins"] = QJsonArray::fromStringList(picked_proteins_);
    j["toppings"] = QJsonArray::fromStringList(picked_toppings_);
    j["sauces"] = QJsonArray::fromStringList(picked_sauces_);
    j["split_bases"] = split_->isChecked();
    if (floor_override_) j["floor_g"] = *floor_override_;
    QJsonObject edits;
    for (const auto &[ingredient, o] : overrides_) {
        const QJsonObject e = override_json(o);
        if (!e.isEmpty()) edits[ingredient] = e;
    }
    j["edits"] = edits;
    j["saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // One file per scenario, named for the brand and the scenario; an existing one of
    // the same name is replaced after asking.
    QString slug = QString("%1 - %2").arg(qs(m->brand), name);
    slug.replace(QRegularExpression(R"([^A-Za-z0-9 ._+-])"), "_");
    QDir().mkpath(scenarios_dir());
    const QString path = QDir(scenarios_dir()).filePath(slug + ".json");
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, "Replace scenario",
                                 QString("A scenario called \"%1\" already exists. Replace it?")
                                     .arg(name)) != QMessageBox::Yes)
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Could not save the scenario",
                             QString("Could not write %1.").arg(path));
        return;
    }
    f.write(QJsonDocument(j).toJson(QJsonDocument::Indented));
    f.close();
    reload_scenarios(name);
}

void MainWindow::apply_scenario(const Scenario &s)
{
    const Menu *m = menu();
    if (!m) return;
    // The recipe first: it sets the pinned weights the edits sit on top of.
    if (const int i = recipe_->findData(s.recipe.isEmpty() ? QString() : s.recipe); i >= 0)
        recipe_->setCurrentIndex(i);

    loading_ = true;
    for (auto [box, name] : {std::pair{base1_, s.base1}, std::pair{base2_, s.base2}}) {
        const int i = box->findData(name);
        box->setCurrentIndex(i >= 0 ? i : (box == base2_ ? 0 : box->currentIndex()));
    }
    // Only what this menu still offers comes back.
    auto offered = [&](const QStringList &names, Kind kind) {
        QStringList out;
        for (const QString &n : names)
            if (m->find(ss(n)) && m->find(ss(n))->kind == kind) out << n;
        return out;
    };
    picked_proteins_ = offered(s.proteins, Kind::Protein);
    picked_toppings_ = offered(s.toppings, Kind::Topping);
    picked_sauces_.clear();
    for (const QString &n : s.sauces)
        if (contains(m->sauces, ss(n))) picked_sauces_ << n;
    split_->setChecked(s.split);
    overrides_ = s.overrides;
    floor_override_ = s.floor_g;
    loading_ = false;
    const Model want = s.adaptive ? Model::Adaptive : s.volume ? Model::VolumeFill : Model::Dynamic;
    if (want != model_) set_model(want);
    on_selection_changed();
}

void MainWindow::delete_scenario()
{
    const int i = scenario_->currentIndex() - 1;
    if (i < 0 || i >= static_cast<int>(scenarios_.size())) return;
    if (QMessageBox::question(this, "Delete scenario",
                              QString("Delete the saved scenario \"%1\"? Its file in "
                                      "data/scenarios is removed.")
                                  .arg(scenarios_[i].name)) != QMessageBox::Yes)
        return;
    QFile::remove(scenarios_[i].file);
    reload_scenarios();
}

void MainWindow::export_spec()
{
    const Menu *m = menu();
    if (!m) return;
    const QString recipe = recipe_->currentIndex() > 0 ? recipe_->currentText()
                                                       : QString("Build your own");
    QString base = QString("%1 - %2 spec.pdf").arg(qs(m->brand), recipe);
    base.replace(QRegularExpression(R"([^A-Za-z0-9 ._+-])"), "_");
    const QString path = QFileDialog::getSaveFileName(
        this, "Export recipe spec", QDir(QDir::homePath()).filePath(base), "PDF (*.pdf)");
    if (path.isEmpty()) return;
    if (write_spec(path)) QMessageBox::information(this, "Spec exported", QString("Saved %1.").arg(path));
    else QMessageBox::warning(this, "Could not export", QString("Could not write %1.").arg(path));
}

bool MainWindow::write_spec(const QString &path)
{
    const Menu *m = menu();
    if (!m) return false;
    const QString recipe = recipe_->currentIndex() > 0 ? recipe_->currentText()
                                                       : QString("Build your own");

    // The page is built as HTML from what is on screen, so it says exactly what the
    // window says. Colours are fixed for paper, not taken from the theme.
    auto esc = [](const QString &t) { return t.toHtmlEscaped(); };
    auto plain = [](QString html) { return html.remove(QRegularExpression("<[^>]*>")); };
    QString html;
    html += "<html><body style='font-family:sans-serif; color:#1d2321; font-size:10pt;'>";
    html += QString("<h2 style='margin:0'>%1 · %2</h2>").arg(esc(qs(m->brand)), esc(recipe));
    html += QString("<p style='color:#5b6663; margin-top:2px'>%1 · %2</p>")
                .arg(adaptive_ ? "Adaptive fill"
                     : model_ == Model::VolumeFill ? "Volume fill" : "Dynamic portioning",
                     QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    html += QString("<p><b>%1</b><br>%2</p>").arg(esc(verdict_head_->text()),
                                                 esc(plain(verdict_sub_->text())));

    // The diagram, drawn in the light palette for paper.
    {
        const bool dark = theme::is_dark();
        theme::set_dark(false);
        spec_image_ = diagram_->grab().toImage();
        theme::set_dark(dark);
        diagram_->update();
        html += "<p><img src='bowl://diagram' width='380'></p>";
    }
    html += QString("<p>%1</p>").arg(fill_note_->text());

    // The portions, as the table on screen has them.
    html += "<table cellspacing='0' cellpadding='4' style='border-collapse:collapse'>";
    html += "<tr>";
    for (int c = 0; c < breakdown_->columnCount() - 1; ++c)
        html += QString("<th align='%1' style='border-bottom:1px solid #999'>%2</th>")
                    .arg(c == 0 ? "left" : "right",
                         esc(breakdown_->horizontalHeaderItem(c)->text()));
    html += "</tr>";
    for (int r = 0; r < breakdown_->rowCount(); ++r) {
        html += "<tr>";
        for (int c = 0; c < breakdown_->columnCount() - 1; ++c) {
            const QTableWidgetItem *it = breakdown_->item(r, c);
            const bool edited = it && it->font().bold();
            html += QString("<td align='%1' style='border-bottom:1px solid #ddd'>%2%3%4</td>")
                        .arg(c == 0 ? "left" : "right", edited ? "<b>" : "",
                             esc(it ? it->text() : QString()), edited ? "</b>" : "");
        }
        html += "</tr>";
    }
    html += "</table>";
    if (row_edited_any())
        html += "<p style='color:#5b6663'>Bold values were edited from the menu's.</p>";

    // The set-up the numbers depend on.
    const BowlGeometry g = geometry();
    QStringList setup;
    setup << QString("Functional fill %1").arg(units::volume(units::to_oz(capacity_->value()), true));
    setup << QString("%1 sauce cup%2%3").arg(sauce_cup_count()).arg(sauce_cup_count() == 1 ? "" : "s")
                 .arg(picked_sauces_.isEmpty() ? QString() : " (" + picked_sauces_.join(", ") + ")");
    if (!adaptive_) setup << QString("weight floor %1 g").arg(floor_->value(), 0, 'f', 0);
    if (model_ == Model::VolumeFill)
        setup << QString("fit within %1% of the food room, stepping down to no less than %2%")
                     .arg(std::round(fit_pct_->value())).arg(std::round(min_pct_->value()));
    else setup << QString("target %1% of the food room").arg(target_fill_->value(), 0, 'f', 0);
    setup << (g.enabled ? QString("fit judged by height, cups %1")
                              .arg(g.cups_pressed ? "pressed in" : "resting on top")
                        : QString("fit judged by volume"));
    setup << QString("%1 measurements").arg(method() == Method::Robot ? "robot"
                                            : method() == Method::Hand ? "hand" : "pooled");
    html += QString("<p><b>Set-up:</b> %1.</p>").arg(esc(setup.join(" · ")));
    if (!estimates_list_.empty()) {
        html += "<p><b>Estimates these numbers rest on:</b></p><ul>";
        for (const Estimate &e : estimates_list_) html += "<li>" + esc(e.text) + "</li>";
        html += "</ul>";
    }
    html += "</body></html>";

    QTextDocument doc;
    doc.addResource(QTextDocument::ImageResource, QUrl("bowl://diagram"), spec_image_);
    doc.setHtml(html);
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::Letter));
    pdf.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    pdf.setResolution(144);
    pdf.setTitle(QString("%1 - %2").arg(qs(m->brand), recipe));
    doc.setPageSize(QSizeF(pdf.width(), pdf.height()));
    QPainter painter;
    if (!painter.begin(&pdf)) return false;
    doc.drawContents(&painter);
    painter.end();
    return true;
}

bool MainWindow::row_edited_any() const
{
    for (const auto &[name, o] : overrides_)
        if (row_edited(name)) return true;
    return false;
}

void MainWindow::show_settings_page(int page)
{
    settings_->setChecked(true);
    if (page >= 0 && page < static_cast<int>(settings_nav_.size())) settings_nav_[page]->click();
}

void MainWindow::update_estimates(const std::vector<BowlItem> &items, const AdaptiveResult *ar)
{
    enum Page { kBowl, kDispense, kMounding, kCompression, kMeasurements, kIngredients,
                kStations };
    std::vector<Estimate> list;
    auto go_page = [this](int page) { return [this, page] { show_settings_page(page); }; };
    const Menu *m = menu();
    const BowlGeometry g = geometry();

    // Curves standing in for measurements the ingredient does not have yet.
    for (const BowlItem &it : items) {
        const CurveChoice c = choose_curve(it.name, it.kind, curves_, method());
        if (c.source != CurveSource::Own)
            list.push_back({QString("%1: not measured, %2").arg(qs(it.name), qs(describe(c))),
                            go_page(kMeasurements)});
    }
    // Manual Entry names no brand carries: their ramp settings are typical, not real.
    if (m && m->brand == kManualEntryBrand)
        for (const BowlItem &it : items)
            if (const Ingredient *ing = m->find(it.name);
                ing && ing->source == WeightSource::Class)
                list.push_back({QString("%1: written in, so its portion and ramp are typical "
                                        "for a %2").arg(qs(it.name), qs(to_string(ing->kind)).toLower()),
                                go_page(kStations)});
    // Heights and slopes no one has measured.
    if (g.enabled) {
        double tallest = 0;
        for (const BowlItem &it : items) {
            tallest = std::max(tallest, it.piece_height_mm);
            const Ingredient *ing = m ? m->find(it.name) : nullptr;
            if (ing && ing->piece_height_placeholder && it.piece_height_mm > 0)
                list.push_back({QString("%1: piece height is a placeholder (%2 mm)")
                                    .arg(qs(it.name)).arg(it.piece_height_mm, 0, 'f', 0),
                                [this] { show_mappings(); }});
        }
        if (tallest > 0 && g.chunk_proud == BowlGeometry::kPlaceholderChunkProud)
            list.push_back({"How far chunks stand proud is a placeholder", go_page(kMounding)});
    }
    if (g.mound && g.mound_slope_deg == BowlGeometry::kPlaceholderMoundSlope) {
        QStringList defaulted;
        for (const BowlItem &it : items)
            if (it.mound_slope_deg < 0) defaulted << qs(it.name);
        if (!defaulted.isEmpty())
            list.push_back({QString("Mound slope is a placeholder (%1)").arg(defaulted.join(", ")),
                            go_page(kMounding)});
    }
    if (!adaptive_ && compress_->isChecked())
        list.push_back({"Compression is extrapolated: no loaded bowls were measured",
                        go_page(kCompression)});
    {
        QStringList unsourced;
        for (const BowlItem &it : items)
            if (costs_.per_kg(it.name) && costs_.source_of(it.name) != CostSource::KitchenOs)
                unsourced << QString("%1 (%2)").arg(qs(it.name), qs(to_string(costs_.source_of(it.name))));
        if (!unsourced.isEmpty())
            list.push_back({QString("Price not from KitchenOS: %1").arg(unsourced.join(", ")), {}});
    }
    if (ar) {
        for (const AdaptiveItem &it : ar->items)
            if (!it.cost_known)
                list.push_back({QString("%1: no price, uses its kind's default").arg(qs(it.name)), {}});
    }
    if (m && recipe_->currentIndex() > 0)
        for (const Recipe &r : m->recipes)
            if (r.name == recipe_->currentData().toString() && r.unweighted() > 0)
                list.push_back({QString("%1 recipe item%2 had no weight in the menu")
                                    .arg(r.unweighted()).arg(r.unweighted() == 1 ? "" : "s"),
                                {}});

    estimates_list_ = std::move(list);
    const int n = static_cast<int>(estimates_list_.size());
    estimates_->setVisible(n > 0);
    estimates_->setText(QString("ⓘ  %1 estimate%2").arg(n).arg(n == 1 ? "" : "s"));
    QStringList tip;
    for (const Estimate &e : estimates_list_) tip << QString("• ") + e.text;
    estimates_->setToolTip("What this bowl's numbers rest on that is not measured yet:\n"
                           + tip.join("\n"));
}

void MainWindow::show_bowl_cost(const std::vector<BowlItem> &items)
{
    const BowlCost c = cost_bowl(items, costs_);
    QLabel *value = stat_value_[kStatTiles - 1];
    // An asterisk when any price is an estimate, a placeholder or a kind default: the
    // tooltip says which.
    value->setText(c.lines.empty() ? QString("—")
                                   : QString("$%1%2").arg(c.total, 0, 'f', 2)
                                         .arg(c.all_kitchenos() ? "" : " *"));
    QStringList tip;
    tip << "Food cost of the finished bowl: each ingredient's final mass at its price in "
           "data/ingredient_costs.csv. Sauce cups are not included.";
    for (const BowlCostLine &l : c.lines) {
        const std::optional<double> kg = costs_.per_kg(l.name);
        tip << QString("• %1: %2 g → $%3 (%4)")
                   .arg(qs(l.name))
                   .arg(std::round(l.grams))
                   .arg(l.cost, 0, 'f', 2)
                   .arg(l.known ? QString("$%1/kg, %2").arg(*kg, 0, 'f', 2).arg(qs(to_string(l.source)))
                                : QString("no price, its kind's default"));
    }
    if (!c.lines.empty() && !c.all_kitchenos())
        tip << QString("%1% of this cost is priced from KitchenOS.")
                   .arg(std::round(c.kitchenos_share() * 100));
    value->setToolTip(tip.join("\n"));
    stat_tile_[kStatTiles - 1]->setToolTip(value->toolTip());
}

void MainWindow::update_settings_summary(const FillUncertainty &u)
{
    QString text = QString("<b>%1</b> \u00B7 %2")
                       .arg(recipe_->currentIndex() > 0 ? recipe_->currentText()
                                                        : QString("Build your own"),
                            verdict_head_->text());
    if (u.valid) {
        const double over = u.median_oz - u.room_oz;
        text += QString(" \u00B7 median bowl %1 %2 the food room \u00B7 %3% chance of "
                        "overfilling")
                    .arg(units::volume(std::fabs(over), true), over >= 0 ? "over" : "under")
                    .arg(std::round(u.p_over_room * 100));
    }
    settings_summary_->setText(text);
}

void MainWindow::update_curve_note()
{
    QStringList bits;
    for (const std::string &n : curves_.ingredients())
        bits << QString("%1 (%2)").arg(qs(n)).arg(curves_.count(n, Method::Pooled));
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
                    .arg(qs(bf.reference)).arg(bf.points)
                    .arg(bf.factor_at_ml(10), 0, 'f', 2).arg(bf.factor_at_ml(50), 0, 'f', 2)
                    .arg(bf.factor_at_ml(200), 0, 'f', 2);
    else if (measurements_.backfill_requested)
        text += QString(" <span style='color:%1'>Backfill is on but there is no usable "
                        "\"test ...\" ladder (%2 readings; %3 needed).</span>")
                    .arg(theme::palette().over.name()).arg(bf.points).arg(kMinReferencePoints);
    if (const int n = measurements_.dropped_total()) {
        QStringList parts;
        for (const auto &[name, count] : measurements_.dropped)
            parts << QString("%1 %2").arg(qs(name)).arg(count);
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
        if (f.failed) bad << qs(f.name);
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
        audit_brand(*m, curves_, method(), legacy_settings(), adaptive_settings(), &costs_,
                    volume_fill_settings());

    const QString assumptions =
        QString("%1 bowl · %2 curves · %3 sauce cup%4 charged against capacity in both "
                "models · adaptive target %5% · volume fill within %6% of the food room, "
                "stepping down to %7% · each recipe uses the floor its own ingredients "
                "match.")
            .arg(units::volume(units::to_oz(capacity_->value()), true))
            .arg(method() == Method::Robot ? "robot" : method() == Method::Hand ? "hand" : "pooled")
            .arg(sauce_cup_count())
            .arg(sauce_cup_count() == 1 ? "" : "s")
            .arg(std::round(target_fill_->value()))
            .arg(std::round(fit_pct_->value()))
            .arg(std::round(min_pct_->value()));

    ReportDialog dialog(audit, assumptions, this);
    connect(&dialog, &ReportDialog::recipe_chosen, this,
            [this, m](const QString &recipe) { select(qs(m->brand), recipe); });
    connect(&dialog, &ReportDialog::full_report_requested, this, [this, m] {
        SweepContext ctx;
        ctx.menus = &menus_;
        ctx.brand = qs(m->brand);
        ctx.curves = &curves_;
        ctx.method = method();
        ctx.legacy = legacy_settings();
        ctx.adaptive = adaptive_settings();
        ctx.volume = volume_fill_settings();
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
    box.setText(qs(CurveSet::format_help()));
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
    measurements_ = load_measurements(ss(measurements_dir()), curves_, options);
    // Fitted for information only, never applied: the model can only squash bases
    // whose own curve compacts (p < 1), so with camera-fitted curves the fit often
    // rests on one or two bowls. Compression and load transfer stay as set.
    squash_ = fit_squash(measurements_.bowls, curves_, method());
    QStringList problems;
    if (measurements_.folder_missing)
        problems << QString("data/measurements does not exist, so no curves are loaded. "
                            "Upload a file to create it.");
    for (const MeasurementFile &f : measurements_.files)
        if (f.failed) problems << QString("data/measurements/%1: %2").arg(qs(f.name), qs(f.problem));
    if (curve_note_) update_curve_note();
    return problems;
}

/// Copies an accepted upload into the store and reloads it, reporting where it went.
QString MainWindow::store_and_reload(const QString &path, QStringList *problems)
{
    std::string err;
    const QString stored = qs(store_measurement_file(ss(path), ss(measurements_dir()), &err));
    if (stored.isEmpty()) {
        QMessageBox::warning(this, "Could not save the file", qs(err));
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
    std::string note_text;
    int added = 0;
    const bool read = check.load_csv(ss(path), &note_text, &added);
    const QString note = qs(note_text);
    if (!read) {
        QMessageBox box(this);
        box.setWindowTitle("Could not read that CSV");
        box.setIcon(QMessageBox::Warning);
        box.setText(note);
        box.setInformativeText(is_curve_csv(ss(path))
                                   ? "Click Show Details for the expected format."
                                   : "This looks like a bowl sheet (a column per "
                                     "ingredient); use Upload bowl data instead.");
        box.setDetailedText(qs(CurveSet::format_help())
                                .replace(QRegularExpression("<[^>]+>"), ""));
        box.exec();
        return;
    }

    const QStringList before = qsl(curves_.ingredients());
    QStringList problems;
    const QString stored = store_and_reload(path, &problems);
    if (stored.isEmpty()) return;
    QStringList fresh;
    for (const QString &n : qsl(curves_.ingredients()))
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
    box.setText(qs(bowl_data_format_help()));
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::on_upload_bowl_data()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Add scanned bowls", asset_dir_ + "/data", "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    BowlSheet sheet;
    std::string err;
    if (!load_bowl_csv(ss(path), sheet, &err, empty_bowl_ml_->value())) {
        QMessageBox box(this);
        box.setWindowTitle("Could not read that CSV");
        box.setIcon(QMessageBox::Warning);
        box.setText(qs(err));
        box.setInformativeText("Click Show Details for the expected format.");
        box.setDetailedText(qs(bowl_data_format_help()).replace(QRegularExpression("<[^>]+>"), ""));
        box.exec();
        return;
    }

    // Store it, then reload everything: the curves refit with this sheet's single-
    // ingredient bowls, and the squash is fitted over every mixed bowl on file.
    const QStringList before = qsl(curves_.ingredients());
    const std::vector<Observation> points = single_ingredient_points(sheet);
    QStringList problems;
    const QString stored = store_and_reload(path, &problems);
    if (stored.isEmpty()) return;
    QStringList fresh;
    for (const QString &n : qsl(curves_.ingredients()))
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
        if (!fit.flat_rate.empty())
            msg += QString("<br>No curve yet, carried at a flat rate: %1.")
                       .arg(qs(join(fit.flat_rate, ", ")));
        msg += "<br><br>Compression and load transfer are unchanged; set them in Settings \u2192 Compression.";
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

void MainWindow::fill_portion_table(const std::vector<BreakdownRow> &rows,
                                    const QStringList &headers,
                                    const std::map<int, QString> &tips)
{
    // A rebuild replaces every cell, so an editor open in one would vanish; reopen it
    // where it was, which is what lets Tab walk from cell to cell.
    QWidget *focus = QApplication::focusWidget();
    const bool editing = focus && focus != breakdown_ && breakdown_->isAncestorOf(focus);
    const int row = breakdown_->currentRow(), column = breakdown_->currentColumn();
    fill_breakdown(breakdown_, headers, tips, rows);
    if (editing && row >= 0 && row < breakdown_->rowCount() && column >= 0
        && column < breakdown_->columnCount()) {
        if (QTableWidgetItem *it = breakdown_->item(row, column);
            it && it->flags().testFlag(Qt::ItemIsEditable)) {
            breakdown_->setCurrentCell(row, column);
            breakdown_->editItem(it);
        }
    }
    bool any = false;
    for (const auto &[name, o] : overrides_) any |= row_edited(name);
    reset_edits_->setVisible(any);
    static_cast<BowlCard *>(bowl_row_)->relayout();
}

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
                                      [&](const auto &s) { return qs(it.name).contains(s.first); });
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
                .arg(qs(it.name)).arg(std::round(it.final_g)).arg(best, 0, 'f', 0)
                .arg(pit->second == "MexicanRice"
                             && !iequals(it.name, "Mexican Rice")
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
    show_bowl_cost(shim.items);
    update_fill_note(shim);
    diagram_->set_result(shim);
    render_photos(shim.items);
    const std::vector<QColor> band = diagram_->item_colours();

    // The nominal portion and max are edited in place; the solver moves the rest.
    std::vector<BreakdownRow> rows;
    const double food_oz = std::max(0.001, r.food_volume_oz);
    for (size_t i = 0; i < r.items.size() && i < shim.items.size(); ++i) {
        const AdaptiveItem &it = r.items[i];
        BreakdownRow row;
        row.key = qs(it.name);
        row.name = qs(it.name);
        if (it.clamped) row.name += "  (clamped)";
        if (!it.cost_known) row.name += "  (~cost)";
        row.kind = ingredient_tip(shim.items[i]);
        row.swatch = i < band.size() ? band[i] : QColor();
        if (it.clamped) row.colour = pal.over;
        row.edited = row_edited(qs(it.name));
        auto plain = [](const QString &text) { TableCell c; c.text = text; return c; };
        row.cells = {
            portion_cell(qs(it.name), Field::Start, it.nominal_g),
            portion_cell(qs(it.name), Field::Max, shim.items[i].max_g),
            plain(QString("%1 g").arg(std::round(it.final_g))),
            plain(QString("%1%2%").arg(it.delta_pct() > 0.05 ? "+" : "")
                      .arg(it.delta_pct(), 0, 'f', 1)),
            plain(units::volume(it.volume_oz, true)),
            plain(QString("%1%").arg(std::round(it.volume_oz / food_oz * 100))),
            plain(QString("$%1").arg(it.cost, 0, 'f', 2)),
            rate_cell(qs(it.name)),
        };
        rows.push_back(row);
    }
    fill_portion_table(rows, {"Ingredient", "Nominal g", "Max g", "Dispensed", "Change",
                              "Volume", "Share", "Cost",
                              QString("%1/100 g").arg(units::suffix())},
                       {{1, "The recipe's portion, which the solver adjusts. Click to edit."},
                        {2, "The most the dispenser will give. Click to edit."},
                        {4, "Dispensed against nominal, inside the ingredient's tolerance"},
                        {8, QString("Volume per 100 g at the nominal portion. Edit it to "
                                    "make the ingredient fluffier or denser.")}});

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
    note += "Prices are from <code>data/ingredient_costs.csv</code>; the Bowl COGS tile says "
            "which came from KitchenOS.";
    foot_note_->setText(note);

    tol_chart_->set_result(r);
    update_settings_summary(r.fill);
    update_estimates(shim.items, &r);


    curve_filter_->set_options(qsl(curves_.ingredients()));
    curve_chart_->set_curves(&curves_, method());
}

}  // namespace bowlfill
