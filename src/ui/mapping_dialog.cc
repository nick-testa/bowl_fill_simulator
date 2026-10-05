#include "mapping_dialog.hh"
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

namespace bowlfill {
namespace {

namespace space = theme::space;

enum Column { kBrand, kRaw, kMapsTo, kKind, kPiece, kColumns };
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

QDoubleSpinBox *spin_in(QTableWidget *table, int row)
{
    QWidget *cell = table->cellWidget(row, kPiece);
    return cell ? cell->findChild<QDoubleSpinBox *>() : nullptr;
}

QString capitalised(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

}  // namespace

MappingDialog::MappingDialog(const std::vector<Menu> &menus, const NameMappings &current,
                             const PieceHeights &pieces, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Ingredients");
    setStyleSheet(theme::stylesheet());
    resize(1120, 700);

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
        "only to chunky ingredients that stand proud of the food; everything else smears.");
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
    for (const Menu &m : menus) brand_->addItem(m.brand, m.brand);
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
            rows_.push_back({m.brand, n.raw, n.automatic, n.guessed_kind, n.kind});

    QSet<QString> names;
    for (const Menu &m : menus)
        for (const MenuName &n : m.names) names << n.name << n.automatic;
    for (const auto &[key, mapping] : current)
        if (!mapping.name.isEmpty()) names << mapping.name;
    names_ = QStringList(names.begin(), names.end());
    std::sort(names_.begin(), names_.end(),
              [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    table_ = new QTableWidget(static_cast<int>(rows_.size()), kColumns);
    table_->setHorizontalHeaderLabels(
        {"Brand", "Name in menu", "Maps to", "Kind", "Piece height"});
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
    h->setSectionResizeMode(kPiece, QHeaderView::Fixed);
    table_->setColumnWidth(kMapsTo, 280);
    table_->setColumnWidth(kKind, 170);
    table_->setColumnWidth(kPiece, 170);
    v->addWidget(table_, 1);
    populate(current, pieces);

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
    connect(search_, &QLineEdit::textChanged, this, &MappingDialog::apply_filter);
    connect(exp, &QPushButton::clicked, this, &MappingDialog::export_csv);
    connect(imp, &QPushButton::clicked, this, &MappingDialog::import_csv);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);

    refresh_state();
}

void MappingDialog::populate(const NameMappings &current, const PieceHeights &pieces)
{
    std::set<std::pair<QString, QString>> listed;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const Row &row = rows_[r];
        listed.insert({row.brand, row.raw});

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
        for (const QString &k : kMappingKinds) kind->addItem(capitalised(k), k);
        kind->setToolTip("Sauce keeps an ingredient out of the topping pool; the simulator "
                         "counts sauce as cups.");
        table_->setCellWidget(r, kKind, in_cell(kind));

        // Piece height, by the name in use. The lowest value means "no entry": a
        // protein then takes the placeholder, anything else smears.
        auto *piece = new QDoubleSpinBox;
        piece->setRange(-1, 200);
        piece->setDecimals(0);
        piece->setSuffix(" mm");
        piece->setButtonSymbols(QAbstractSpinBox::NoButtons);
        piece->setSpecialValueText(
            row.kind == "protein"
                ? QString("%1 mm (placeholder)").arg(kPlaceholderProteinPieceMm, 0, 'f', 0)
                : QString("Smears"));
        piece->setToolTip("Height of one piece, for chunky ingredients that stand proud of "
                          "the food. 0 mm means it smears. Shared by every row that maps "
                          "to the same name.");
        table_->setCellWidget(r, kPiece, in_cell(piece));

        auto it = current.find({row.brand, row.raw});
        set_row(r, it != current.end() ? it->second : NameMapping{});
        auto pit = pieces.find(name_at(r));
        {
            const QSignalBlocker block(piece);
            piece->setValue(pit != pieces.end() ? pit->second : -1);
        }
        connect(piece, &QDoubleSpinBox::valueChanged, this, [this, r] { sync_piece(r); });

        connect(maps, &QComboBox::currentTextChanged, this, &MappingDialog::refresh_state);
        connect(kind, &QComboBox::currentIndexChanged, this, &MappingDialog::refresh_state);
    }
    for (const auto &[key, mapping] : current)
        if (!listed.count(key)) stale_[key] = mapping;
    for (const auto &[name, mm] : pieces) {
        bool used = false;
        for (int r = 0; r < static_cast<int>(rows_.size()) && !used; ++r)
            used = name_at(r) == name;
        if (!used) stale_pieces_[name] = mm;
    }
}

double MappingDialog::piece_at(int row) const
{
    QDoubleSpinBox *sp = spin_in(table_, row);
    return sp ? sp->value() : -1;
}

void MappingDialog::sync_piece(int row)
{
    // A height belongs to the ingredient, not the spelling: every row mapped to the
    // same name shows the same value.
    const QString name = name_at(row);
    const double mm = piece_at(row);
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (r != row && name_at(r) == name)
            if (QDoubleSpinBox *sp = spin_in(table_, r)) {
                const QSignalBlocker block(sp);
                sp->setValue(mm);
            }
}

PieceHeights MappingDialog::pieces() const
{
    PieceHeights out = stale_pieces_;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        if (piece_at(r) >= 0) out[name_at(r)] = piece_at(r);
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
        maps->setCurrentText(m.name.isEmpty() ? rows_[row].automatic : m.name);
    }
    if (auto *kind = combo_in(table_, row, kKind)) {
        const QSignalBlocker block(kind);
        const int i = kind->findData(m.kind);
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
        const auto key = std::make_pair(rows_[r].brand, rows_[r].raw);
        out.erase(key);
        if (!is_mapped(r)) continue;
        const QString name = name_at(r);
        out[key] = {name == rows_[r].automatic ? QString() : name, kind_at(r)};
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
        all[{rows_[r].brand, rows_[r].raw}] = {name_at(r), kind_at(r)};
    QString err;
    if (!save_name_mappings(path, all, &err))
        QMessageBox::warning(this, "Could not export", err);
}

void MappingDialog::import_csv()
{
    const QString path = QFileDialog::getOpenFileName(this, "Import ingredient names", {},
                                                      "CSV (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    NameMappings read;
    QString err;
    if (!load_name_mappings(path, read, &err)) {
        QMessageBox::warning(this, "Could not import", err);
        return;
    }
    int applied = 0;
    std::set<std::pair<QString, QString>> matched;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        auto it = read.find({rows_[r].brand, rows_[r].raw});
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
