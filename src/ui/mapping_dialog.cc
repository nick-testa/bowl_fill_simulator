#include "mapping_dialog.hh"
#include "qt_text.hh"
#include "theme.hh"

#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace bowlfill {
namespace {

namespace space = theme::space;

enum Column { kBrand, kRaw, kMapsTo, kKind, kGroup, kPiece, kSlope, kRoll, kColumns };
enum Show { kAll, kMapped, kUnmapped };

/// Cell widgets fill their cell edge to edge; this insets one so rows breathe.
QWidget *in_cell(QWidget *w)
{
    auto *box = new QWidget;
    box->setObjectName("clear");
    auto *h = new QHBoxLayout(box);
    h->setContentsMargins(0, 4, theme::space::sm, 4);
    h->addWidget(w);
    return box;
}

QComboBox *combo_in(QTableWidget *table, int row, int col)
{
    QWidget *cell = table->cellWidget(row, col);
    return cell ? cell->findChild<QComboBox *>() : nullptr;
}

QDoubleSpinBox *spin_in(QTableWidget *table, int row, int col = kPiece)
{
    QWidget *cell = table->cellWidget(row, col);
    return cell ? cell->findChild<QDoubleSpinBox *>() : nullptr;
}

QString capitalised(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

}  // namespace

MappingDialog::MappingDialog(const std::vector<Menu> &menus, const NameMappings &current,
                             const PieceHeights &pieces, const MoundSlopes &slopes,
                             const RollOffAngles &rolls, const IngredientGroups &groups,
                             const QString &brand,
                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Ingredients");
    setStyleSheet(theme::stylesheet());
    resize(1280, 700);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(space::xl, space::lg, space::xl, space::lg);
    v->setSpacing(space::md);

    auto *head = new QLabel("Map each menu's spelling onto a common name");
    head->setObjectName("heading");
    v->addWidget(head);
    auto *blurb = new QLabel(
        "Costs, measured curves, colours and photos are all looked up by name, so two "
        "brands that call one ingredient different things only share that data once "
        "both map to the same name. Leave <i>Maps to</i> as it is to keep the automatic "
        "name; set <i>Kind</i> only to correct a wrong guess. Give a <i>Piece height</i> "
        "only to chunky ingredients that stand proud of the food; everything else smears. "
        "A <i>Mound slope</i> sets how steeply an ingredient piles when mounding is "
        "shown; leave it on <i>Default</i> to use the default slope in Settings \u2192 Mounding & chunks. "
        "Give chunky pieces that roll off a mound a <i>Roll-off</i> angle: they roll down "
        "anything steeper and ring the mound's foot instead of piling on top. "
        "A <i>Pool group</i> sets which ingredients share a typical curve in the fitted "
        "curves; type a new name to start a group of your own.");
    blurb->setObjectName("lead");
    blurb->setWordWrap(true);
    v->addWidget(blurb);

    // ---- filters ----------------------------------------------------------
    auto *filters = new QHBoxLayout;
    filters->setSpacing(space::sm);
    show_ = new QComboBox;
    show_->addItems({"All names", "Mapped", "Not mapped"});
    brand_ = new QComboBox;
    brand_->addItem("All brands", QString());
    for (const Menu &m : menus) brand_->addItem(qs(m.brand), qs(m.brand));
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search names");
    search_->setClearButtonEnabled(true);
    filters->addWidget(show_);
    filters->addWidget(brand_);
    filters->addWidget(search_, 1);
    v->addLayout(filters);

    // ---- the table --------------------------------------------------------
    for (const Menu &m : menus)
        for (const MenuName &n : m.names)
            rows_.push_back({qs(m.brand), qs(n.raw), qs(n.automatic), qs(n.guessed_kind), qs(n.kind)});

    QSet<QString> names;
    for (const Menu &m : menus)
        for (const MenuName &n : m.names) names << qs(n.name) << qs(n.automatic);
    for (const auto &[key, mapping] : current)
        if (!mapping.name.empty()) names << qs(mapping.name);
    names_ = QStringList(names.begin(), names.end());
    std::sort(names_.begin(), names_.end(),
              [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    table_ = new QTableWidget(static_cast<int>(rows_.size()), kColumns);
    table_->setHorizontalHeaderLabels(
        {"Brand", "Name in menu", "Maps to", "Kind", "Pool group", "Piece height", "Mound slope",
         "Roll-off"});
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(40);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    QHeaderView *h = table_->horizontalHeader();
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    h->setSectionResizeMode(kBrand, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(kRaw, QHeaderView::Stretch);
    h->setSectionResizeMode(kMapsTo, QHeaderView::Fixed);
    h->setSectionResizeMode(kKind, QHeaderView::Fixed);
    h->setSectionResizeMode(kGroup, QHeaderView::Fixed);
    h->setSectionResizeMode(kPiece, QHeaderView::Fixed);
    h->setSectionResizeMode(kSlope, QHeaderView::Fixed);
    h->setSectionResizeMode(kRoll, QHeaderView::Fixed);
    table_->setColumnWidth(kMapsTo, 280);
    table_->setColumnWidth(kKind, 170);
    table_->setColumnWidth(kGroup, 190);
    table_->setColumnWidth(kPiece, 170);
    table_->setColumnWidth(kSlope, 130);
    table_->setColumnWidth(kRoll, 110);
    v->addWidget(table_, 1);
    populate(current, pieces, slopes, rolls, groups);

    // ---- footer -----------------------------------------------------------
    auto *foot = new QHBoxLayout;
    foot->setSpacing(space::sm);
    auto *exp = new QPushButton("Export CSV…");
    exp->setToolTip("Every row, mapped or not, as brand, ingredient_menu, ingredient_name, "
                    "kind. Fill in ingredient_name (and kind if needed), then import it.");
    auto *imp = new QPushButton("Import CSV…");
    imp->setToolTip("Apply a filled-in table. Rows it does not mention are left as they are.");
    count_ = new QLabel;
    count_->setObjectName("note");
    auto *cancel = new QPushButton("Cancel");
    auto *save = new QPushButton("Save");
    save->setObjectName("primary");
    save->setDefault(true);
    foot->addWidget(exp);
    foot->addWidget(imp);
    foot->addSpacing(space::md);
    foot->addWidget(count_, 1);
    foot->addWidget(cancel);
    foot->addWidget(save);
    v->addLayout(foot);

    connect(show_, &QComboBox::currentIndexChanged, this, &MappingDialog::apply_filter);
    connect(brand_, &QComboBox::currentIndexChanged, this, &MappingDialog::apply_filter);
    // Opens on the brand being worked on; "All brands" is one click away.
    if (const int i = brand_->findData(brand); !brand.isEmpty() && i >= 0)
        brand_->setCurrentIndex(i);
    connect(search_, &QLineEdit::textChanged, this, &MappingDialog::apply_filter);
    connect(exp, &QPushButton::clicked, this, &MappingDialog::export_csv);
    connect(imp, &QPushButton::clicked, this, &MappingDialog::import_csv);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);

    refresh_state();
}

void MappingDialog::populate(const NameMappings &current, const PieceHeights &pieces,
                             const MoundSlopes &slopes, const RollOffAngles &rolls,
                             const IngredientGroups &groups)
{
    // Every group on offer: the automatic ones, then any of the user's own.
    QStringList group_names;
    for (const std::string &g : kAutomaticGroups) group_names << qs(g);
    for (const auto &[name, g] : groups)
        if (!group_names.contains(qs(g))) group_names << qs(g);

    std::set<std::pair<std::string, std::string>> listed;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const Row &row = rows_[r];
        listed.insert({ss(row.brand), ss(row.raw)});

        auto *brand = new QTableWidgetItem(row.brand);
        brand->setForeground(theme::palette().ink_soft);
        table_->setItem(r, kBrand, brand);
        auto *raw = new QTableWidgetItem(row.raw);
        raw->setToolTip(QString("Automatic name: %1\nGuessed kind: %2")
                            .arg(row.automatic, row.guessed_kind));
        table_->setItem(r, kRaw, raw);

        auto *maps = new QComboBox;
        maps->setEditable(true);
        maps->addItems(names_);
        maps->setInsertPolicy(QComboBox::NoInsert);
        maps->setMaxVisibleItems(16);
        maps->completer()->setCompletionMode(QCompleter::PopupCompletion);
        maps->completer()->setFilterMode(Qt::MatchContains);
        maps->completer()->setCaseSensitivity(Qt::CaseInsensitive);
        maps->setToolTip("The name this spelling is offered under. Pick an existing name "
                         "to merge it with another brand's ingredient.");
        table_->setCellWidget(r, kMapsTo, in_cell(maps));

        auto *kind = new QComboBox;
        kind->addItem(QString("Guess: %1").arg(row.guessed_kind), QString());
        for (const std::string &k : kMappingKinds) kind->addItem(capitalised(qs(k)), qs(k));
        kind->setToolTip("Sauce keeps an ingredient out of the topping pool; the simulator "
                         "counts sauce as cups.");
        table_->setCellWidget(r, kKind, in_cell(kind));

        // Pool group, by the name in use. The first entry keeps the automatic group;
        // typing a new name starts a group of its own.
        auto *group = new QComboBox;
        group->setEditable(true);
        group->setInsertPolicy(QComboBox::NoInsert);
        group->addItem(QString("Automatic"));   // labelled once the row's name is known
        group->addItems(group_names);
        group->setToolTip("Ingredients in a group share a typical curve, so one with few or no "
                          "measurements borrows from the others. Shared by every row that maps "
                          "to the same name.");
        table_->setCellWidget(r, kGroup, in_cell(group));

        // Piece height, by the name in use. The lowest value means "no entry": a
        // protein then takes the placeholder, anything else smears.
        auto *piece = new QDoubleSpinBox;
        piece->setRange(-1, 200);
        piece->setDecimals(0);
        piece->setSuffix(" mm");
        theme::bare_spin(piece);
        piece->setSpecialValueText(
            row.kind == "protein"
                ? QString("%1 mm (placeholder)").arg(kPlaceholderProteinPieceMm, 0, 'f', 0)
                : QString("Smears"));
        piece->setToolTip("Height of one piece, for chunky ingredients that stand proud of "
                          "the food. 0 mm means it smears. Shared by every row that maps "
                          "to the same name.");
        table_->setCellWidget(r, kPiece, in_cell(piece));

        // Mound slope, likewise by the name in use; the lowest value is "Default".
        auto *slope = new QDoubleSpinBox;
        slope->setRange(-1, 80);
        slope->setDecimals(0);
        slope->setSuffix("\u00B0");
        theme::bare_spin(slope);
        slope->setSpecialValueText("Default");
        slope->setToolTip("How steeply it piles when mounding is shown (its angle of "
                          "repose). 0\u00B0 levels out flat. Shared by every row that maps "
                          "to the same name.");
        table_->setCellWidget(r, kSlope, in_cell(slope));

        // Roll-off, likewise; the lowest value is "None": it stays where it lands.
        auto *roll = new QDoubleSpinBox;
        roll->setRange(-1, 80);
        roll->setDecimals(0);
        roll->setSuffix("\u00B0");
        theme::bare_spin(roll);
        roll->setSpecialValueText("None");
        roll->setToolTip("For chunky pieces that roll: they roll down any food steeper "
                         "than this and pile where it flattens out, usually round the "
                         "foot of the mound. None: it stays where it lands. Shared by "
                         "every row that maps to the same name.");
        table_->setCellWidget(r, kRoll, in_cell(roll));

        auto it = current.find({ss(row.brand), ss(row.raw)});
        set_row(r, it != current.end() ? it->second : NameMapping{});
        auto pit = pieces.find(ss(name_at(r)));
        {
            const QSignalBlocker block(piece);
            piece->setValue(pit != pieces.end() ? pit->second : -1);
        }
        connect(piece, &QDoubleSpinBox::valueChanged, this, [this, r] { sync_piece(r); });
        auto sit = slopes.find(ss(name_at(r)));
        {
            const QSignalBlocker block(slope);
            slope->setValue(sit != slopes.end() ? sit->second : -1);
        }
        connect(slope, &QDoubleSpinBox::valueChanged, this,
                [this, r] { sync_shared(r, kSlope); });
        auto rit = rolls.find(ss(name_at(r)));
        {
            const QSignalBlocker block(roll);
            roll->setValue(rit != rolls.end() ? rit->second : -1);
        }
        connect(roll, &QDoubleSpinBox::valueChanged, this,
                [this, r] { sync_shared(r, kRoll); });
        {
            const QSignalBlocker block(group);
            group->setItemText(0, QString("Automatic: %1").arg(qs(automatic_group(ss(name_at(r))))));
            auto git = std::find_if(groups.begin(), groups.end(), [&](const auto &e) {
                return QString::compare(qs(e.first), name_at(r), Qt::CaseInsensitive) == 0;
            });
            if (git != groups.end()) group->setCurrentText(qs(git->second));
            else group->setCurrentIndex(0);
        }
        connect(group, &QComboBox::currentTextChanged, this, [this, r] { sync_group(r); });

        connect(maps, &QComboBox::currentTextChanged, this, &MappingDialog::refresh_state);
        connect(kind, &QComboBox::currentIndexChanged, this, &MappingDialog::refresh_state);
    }
    for (const auto &[key, mapping] : current)
        if (!listed.count(key)) stale_[key] = mapping;
    for (const auto &[name, mm] : pieces) {
        bool used = false;
        for (int r = 0; r < static_cast<int>(rows_.size()) && !used; ++r)
            used = ss(name_at(r)) == name;
        if (!used) stale_pieces_[name] = mm;
    }
    for (const auto &[name, deg] : slopes) {
        bool used = false;
        for (int r = 0; r < static_cast<int>(rows_.size()) && !used; ++r)
            used = ss(name_at(r)) == name;
        if (!used) stale_slopes_[name] = deg;
    }
    for (const auto &[name, deg] : rolls) {
        bool used = false;
        for (int r = 0; r < static_cast<int>(rows_.size()) && !used; ++r)
            used = ss(name_at(r)) == name;
        if (!used) stale_rolls_[name] = deg;
    }
    for (const auto &[name, g] : groups) {
        bool used = false;
        for (int r = 0; r < static_cast<int>(rows_.size()) && !used; ++r)
            used = QString::compare(name_at(r), qs(name), Qt::CaseInsensitive) == 0;
        if (!used) stale_groups_[name] = g;
    }
}

QString MappingDialog::group_at(int row) const
{
    QComboBox *group = combo_in(table_, row, kGroup);
    const QString text = group ? group->currentText().trimmed().toLower() : QString();
    return text.startsWith("automatic") ? QString() : text;
}

void MappingDialog::sync_group(int row)
{
    // A group belongs to the ingredient, not the spelling, like a piece height.
    const QString name = name_at(row);
    QComboBox *source = combo_in(table_, row, kGroup);
    if (!source) return;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (r != row && name_at(r) == name)
            if (QComboBox *g = combo_in(table_, r, kGroup)) {
                const QSignalBlocker block(g);
                g->setCurrentText(source->currentText());
            }
}

IngredientGroups MappingDialog::groups() const
{
    IngredientGroups out = stale_groups_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const QString g = group_at(r);
        if (g.isEmpty() || ss(g) == automatic_group(ss(name_at(r)))) continue;
        out[ss(name_at(r))] = ss(g);
    }
    return out;
}

double MappingDialog::piece_at(int row) const
{
    QDoubleSpinBox *sp = spin_in(table_, row);
    return sp ? sp->value() : -1;
}

void MappingDialog::sync_piece(int row)
{
    sync_shared(row, kPiece);
}

void MappingDialog::sync_shared(int row, int column)
{
    // A height or slope belongs to the ingredient, not the spelling: every row mapped
    // to the same name shows the same value.
    const QString name = name_at(row);
    QDoubleSpinBox *source = spin_in(table_, row, column);
    if (!source) return;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (r != row && name_at(r) == name)
            if (QDoubleSpinBox *sp = spin_in(table_, r, column)) {
                const QSignalBlocker block(sp);
                sp->setValue(source->value());
            }
}

RollOffAngles MappingDialog::rolls() const
{
    RollOffAngles out = stale_rolls_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (QDoubleSpinBox *sp = spin_in(table_, r, kRoll); sp && sp->value() >= 0)
            out[ss(name_at(r))] = sp->value();
    return out;
}

MoundSlopes MappingDialog::slopes() const
{
    MoundSlopes out = stale_slopes_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (QDoubleSpinBox *sp = spin_in(table_, r, kSlope); sp && sp->value() >= 0)
            out[ss(name_at(r))] = sp->value();
    return out;
}

PieceHeights MappingDialog::pieces() const
{
    PieceHeights out = stale_pieces_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (piece_at(r) >= 0) out[ss(name_at(r))] = piece_at(r);
    return out;
}

QString MappingDialog::name_at(int row) const
{
    auto *maps = combo_in(table_, row, kMapsTo);
    const QString text = maps ? maps->currentText().trimmed() : QString();
    return text.isEmpty() ? rows_[row].automatic : text;
}

QString MappingDialog::kind_at(int row) const
{
    auto *kind = combo_in(table_, row, kKind);
    return kind ? kind->currentData().toString() : QString();
}

void MappingDialog::set_row(int row, const NameMapping &m)
{
    if (auto *maps = combo_in(table_, row, kMapsTo)) {
        const QSignalBlocker block(maps);
        maps->setCurrentText(m.name.empty() ? rows_[row].automatic : qs(m.name));
    }
    if (auto *kind = combo_in(table_, row, kKind)) {
        const QSignalBlocker block(kind);
        const int i = kind->findData(qs(m.kind));
        kind->setCurrentIndex(i >= 0 ? i : 0);
    }
}

bool MappingDialog::is_mapped(int row) const
{
    return name_at(row) != rows_[row].automatic || !kind_at(row).isEmpty();
}

void MappingDialog::refresh_state()
{
    const theme::Palette &pal = theme::palette();
    int mapped = 0;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const bool on = is_mapped(r);
        mapped += on;
        // A mapped row's menu spelling reads in the accent, so edits stand out.
        if (QTableWidgetItem *raw = table_->item(r, kRaw)) {
            raw->setForeground(on ? pal.accent : pal.ink);
            QFont f = raw->font();
            f.setWeight(on ? QFont::DemiBold : QFont::Normal);
            raw->setFont(f);
        }
    }
    count_->setText(QString("%1 of %2 spellings mapped").arg(mapped).arg(rows_.size()));
    apply_filter();
}

void MappingDialog::apply_filter()
{
    const QString brand = brand_->currentData().toString();
    const QString needle = search_->text().trimmed();
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const Row &row = rows_[r];
        bool show = brand.isEmpty() || row.brand == brand;
        if (show && show_->currentIndex() == kMapped) show = is_mapped(r);
        if (show && show_->currentIndex() == kUnmapped) show = !is_mapped(r);
        if (show && !needle.isEmpty())
            show = row.raw.contains(needle, Qt::CaseInsensitive)
                   || name_at(r).contains(needle, Qt::CaseInsensitive);
        table_->setRowHidden(r, !show);
    }
}

NameMappings MappingDialog::mappings() const
{
    NameMappings out = stale_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const auto key = std::make_pair(ss(rows_[r].brand), ss(rows_[r].raw));
        out.erase(key);
        if (!is_mapped(r)) continue;
        const QString name = name_at(r);
        out[key] = {name == rows_[r].automatic ? std::string() : ss(name), ss(kind_at(r))};
    }
    return out;
}

void MappingDialog::export_csv()
{
    const QString path = QFileDialog::getSaveFileName(this, "Export ingredient names",
                                                      "ingredient-names.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    // Every row, with the name in use filled in, so the sheet can be edited in place.
    NameMappings all = stale_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        all[{ss(rows_[r].brand), ss(rows_[r].raw)}] = {ss(name_at(r)), ss(kind_at(r))};
    std::string err;
    if (!save_name_mappings(ss(path), all, &err))
        QMessageBox::warning(this, "Could not export", qs(err));
}

void MappingDialog::import_csv()
{
    const QString path = QFileDialog::getOpenFileName(this, "Import ingredient names", {},
                                                      "CSV (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    NameMappings read;
    std::string err;
    if (!load_name_mappings(ss(path), read, &err)) {
        QMessageBox::warning(this, "Could not import", qs(err));
        return;
    }
    int applied = 0;
    std::set<std::pair<std::string, std::string>> matched;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        auto it = read.find({ss(rows_[r].brand), ss(rows_[r].raw)});
        if (it == read.end()) continue;
        set_row(r, it->second);
        matched.insert(it->first);
        ++applied;
    }
    int unknown = 0;
    for (const auto &[key, mapping] : read)
        if (!matched.count(key)) {
            stale_[key] = mapping;
            ++unknown;
        }
    refresh_state();

    QString msg = QString("Applied %1 row%2.").arg(applied).arg(applied == 1 ? "" : "s");
    if (unknown)
        msg += QString(" %1 row%2 named a spelling no current menu uses; kept for when it "
                       "comes back.").arg(unknown).arg(unknown == 1 ? "" : "s");
    msg += " Nothing is written until you press Save.";
    QMessageBox::information(this, "Imported", msg);
}

}  // namespace bowlfill
