#include "report_dialog.hh"
#include "theme.hh"
#include "units.hh"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace bowlfill {
namespace {

/// Short status for the table's Legacy column; the sentence goes in the detail pane.
QString legacy_tag(const RecipeAudit &r)
{
    if (r.customer_built) return "customer-built";
    switch (r.legacy.verdict) {
    case Verdict::OverAtStart: return "overflows";
    case Verdict::OverWhileRamping: return "ramps over";
    case Verdict::Saturated: return "never fills";
    case Verdict::NoFloor: return r.legacy.crossed_at_g ? "over, no floor" : "no floor";
    case Verdict::FitsMassBound: return "fits";
    }
    return {};
}

QString adaptive_tag(const RecipeAudit &r)
{
    if (r.customer_built) return "—";
    switch (r.adaptive.status) {
    case AdaptiveStatus::NoAdjustment: return "fits, unchanged";
    case AdaptiveStatus::Adjusted: return "fits, adjusted";
    case AdaptiveStatus::StillOver: return "still over";
    case AdaptiveStatus::Underfilled: return "underfills";
    case AdaptiveStatus::NoIngredients: return "—";
    }
    return {};
}

QColor severity_colour(const RecipeAudit &r)
{
    const theme::Palette &pal = theme::palette();
    if (r.customer_built) return pal.ink_faint;
    return r.severity() >= 3 ? pal.over : r.severity() > 0 ? pal.ink_soft : pal.ink;
}

QTableWidgetItem *cell(const QString &text, const QColor &fg,
                       Qt::Alignment align = Qt::AlignLeft)
{
    auto *it = new QTableWidgetItem(text);
    it->setForeground(fg);
    it->setTextAlignment(align | Qt::AlignVCenter);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return it;
}

}  // namespace

ReportDialog::ReportDialog(const BrandAudit &audit, const QString &assumptions,
                           QWidget *parent)
    : QDialog(parent), audit_(audit)
{
    setWindowTitle(QString("Recipe feasibility — %1").arg(audit_.brand));
    setStyleSheet(theme::stylesheet());
    resize(1100, 620);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(10);

    auto *head = new QLabel(audit_.headline());
    head->setFont(theme::font(theme::Text::Heading, true));
    head->setWordWrap(true);
    v->addWidget(head);

    auto *note = new QLabel(assumptions);
    note->setObjectName("note");
    note->setWordWrap(true);
    v->addWidget(note);

    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(
        {"Recipe", "Kind", "Pins", "As written (g)",
         QString("As written (%1)").arg(units::suffix()), "Legacy ramp", "Adaptive"});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    table_->setFont(theme::font(theme::Text::Body));
    v->addWidget(table_, 1);

    detail_ = new QLabel;
    detail_->setWordWrap(true);
    detail_->setMinimumHeight(56);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    v->addWidget(detail_);

    populate();

    auto *buttons = new QDialogButtonBox(this);
    auto *open = buttons->addButton("Open in simulator", QDialogButtonBox::ActionRole);
    open->setObjectName("primary");
    auto *full = buttons->addButton("Full report…", QDialogButtonBox::ActionRole);
    full->setToolTip("Sweep every orderable combination of bases, proteins and toppings "
                     "against every recipe in this brand, and save it as CSV.");
    auto *copy = buttons->addButton("Copy CSV", QDialogButtonBox::ActionRole);
    auto *save = buttons->addButton("Save CSV…", QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    v->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(full, &QPushButton::clicked, this, [this] { emit full_report_requested(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(audit_.to_csv());
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save feasibility report",
            QString("%1-feasibility.csv").arg(QString(audit_.brand).replace(' ', '-').toLower()),
            "CSV (*.csv)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Could not save", QString("Cannot write %1").arg(path));
            return;
        }
        QTextStream(&f) << audit_.to_csv();
    });

    auto emit_current = [this] {
        const int row = table_->currentRow();
        if (row < 0 || row >= static_cast<int>(audit_.rows.size())) return;
        emit recipe_chosen(audit_.rows[row].recipe);
        accept();
    };
    connect(open, &QPushButton::clicked, this, emit_current);
    connect(table_, &QTableWidget::itemDoubleClicked, this, emit_current);
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { show_detail(row); });

    if (!audit_.rows.empty()) table_->selectRow(0);
}

void ReportDialog::populate()
{
    table_->setRowCount(static_cast<int>(audit_.rows.size()));
    for (int i = 0; i < static_cast<int>(audit_.rows.size()); ++i) {
        const RecipeAudit &r = audit_.rows[i];
        const QColor fg = severity_colour(r);
        // The selection highlight overrides the foreground, so severity is carried in
        // weight as well as colour.
        QFont row_font = theme::font(theme::Text::Body, r.severity() >= 3);
        const QString kind = r.customer_built ? "customer-built"
                             : r.complete_bowl ? "complete"
                                               : "partial";
        table_->setItem(i, 0, cell(r.recipe, fg));
        table_->setItem(i, 1, cell(kind, theme::palette().ink_soft));
        table_->setItem(i, 2, cell(QString::number(r.pinned), fg, Qt::AlignRight));
        table_->setItem(i, 3, cell(r.customer_built ? "—" : QString::number(r.nominal_g, 'f', 0) + " g",
                                   fg, Qt::AlignRight));
        table_->setItem(i, 4, cell(r.customer_built ? "—" : units::volume(r.nominal_oz), fg,
                                   Qt::AlignRight));
        table_->setItem(i, 5, cell(legacy_tag(r), fg));
        table_->setItem(i, 6, cell(adaptive_tag(r), fg));
        for (int c = 0; c < table_->columnCount(); ++c) table_->item(i, c)->setFont(row_font);
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

void ReportDialog::show_detail(int row)
{
    if (row < 0 || row >= static_cast<int>(audit_.rows.size())) {
        detail_->clear();
        return;
    }
    const RecipeAudit &r = audit_.rows[row];
    const theme::Palette &pal = theme::palette();
    QString html = QString("<b style='color:%1'>%2</b><br>")
                       .arg(severity_colour(r).name(), r.recipe.toHtmlEscaped());
    html += QString("<span style='color:%1'>Legacy ramp — %2</span><br>")
                .arg(pal.ink.name(), r.legacy_summary().toHtmlEscaped());
    if (!r.customer_built)
        html += QString("<span style='color:%1'>Adaptive — %2</span>")
                    .arg(pal.ink_soft.name(), r.adaptive_summary().toHtmlEscaped());
    if (!r.customer_built && !r.complete_bowl)
        html += QString("<br><span style='color:%1'>Pins only %2 ingredient%3, so this is "
                        "a modifier rather than a finished bowl.</span>")
                    .arg(pal.ink_faint.name())
                    .arg(r.pinned)
                    .arg(r.pinned == 1 ? "" : "s");
    detail_->setText(html);
}

}  // namespace bowlfill
