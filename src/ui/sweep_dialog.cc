#include "sweep_dialog.hh"
#include "theme.hh"

#include <QComboBox>
#include <QPoint>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace bowlfill {
namespace {

/// Measured on this machine at 268k rows/s; only used to warn about a long run.
constexpr double kRowsPerSecond = 250000.0;

QString human_bytes(qint64 b)
{
    if (b < 1024LL * 1024) return QString("%1 KB").arg(b / 1024);
    if (b < 1024LL * 1024 * 1024) return QString("%1 MB").arg(b / (1024 * 1024));
    return QString("%1 GB").arg(double(b) / (1024.0 * 1024 * 1024), 0, 'f', 1);
}

QString human_time(double seconds)
{
    if (seconds < 90) return QString("%1 s").arg(std::max(1.0, seconds), 0, 'f', 0);
    return QString("%1 min").arg(seconds / 60.0, 0, 'f', seconds < 600 ? 1 : 0);
}

constexpr int kDialogWidth = 700;
constexpr int kMargin = 18;
constexpr int kTextWidth = kDialogWidth - 2 * kMargin;

/// A wrapped QLabel inside a vertical layout reports a one-line height, so the dialog
/// sizes itself to that and clips the rest. Against a fixed dialog width the wrapped
/// height is knowable, so set it outright.
void fit_wrapped(QLabel *label)
{
    label->setWordWrap(true);
    label->setMinimumHeight(label->heightForWidth(kTextWidth));
}

}  // namespace

SweepDialog::SweepDialog(SweepContext context, QWidget *parent)
    : QDialog(parent), ctx_(std::move(context))
{
    setWindowTitle("Full combinatorial report");
    setStyleSheet(theme::stylesheet());
    setFixedWidth(kDialogWidth);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(kMargin, 16, kMargin, 16);
    v->setSpacing(10);

    auto *head = new QLabel("Full combinatorial report");
    head->setFont(theme::font(theme::Text::Heading, true));
    v->addWidget(head);

    auto *blurb = new QLabel(
        "One row per named recipe per orderable combination. Every bowl carries at least "
        "one base, one protein, one topping and one sauce, because the menu will not let "
        "a customer order otherwise; second bases, proteins and sauces are swept too. The "
        "recipe's pinned weights are kept wherever it still specifies that ingredient; "
        "anything substituted in uses its own menu portion. The 50:50 base split and base "
        "compression are on for every bowl, and both models charge the sauce cups against "
        "capacity.");
    blurb->setObjectName("note");
    fit_wrapped(blurb);
    v->addWidget(blurb);

    scope_ = new QComboBox;
    scope_->addItem(QString("This brand — %1").arg(ctx_.brand), ctx_.brand);
    scope_->addItem("Every brand", QString());
    v->addWidget(scope_);

    auto *caps = new QHBoxLayout;
    caps->setSpacing(10);
    max_toppings_ = new QSpinBox;
    max_toppings_->setRange(0, 20);
    max_toppings_->setValue(4);
    max_proteins_ = new QSpinBox;
    max_proteins_->setRange(0, 6);
    max_proteins_->setValue(2);
    auto labelled = [](const QString &caption, QWidget *w) {
        auto *box = new QWidget;
        box->setObjectName("clear");
        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(3);
        auto *l = new QLabel(caption);
        l->setObjectName("label");
        bv->addWidget(l);
        bv->addWidget(w);
        return box;
    };
    sauces_ = new QComboBox;
    sauces_->addItem("one and two — sweep both", QPoint(1, 2));
    sauces_->addItem("always one — included", QPoint(1, 1));
    sauces_->addItem("always two — with upcharge", QPoint(2, 2));
    caps->addWidget(labelled("max toppings per bowl", max_toppings_), 1);
    caps->addWidget(labelled("max proteins per bowl", max_proteins_), 1);
    caps->addWidget(labelled("sauce cups", sauces_), 2);
    v->addLayout(caps);

    estimate_ = new QLabel;
    estimate_->setObjectName("lead");
    estimate_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(estimate_);

    warning_ = new QLabel;
    warning_->setWordWrap(true);
    v->addWidget(warning_);

    auto *buttons = new QDialogButtonBox(this);
    auto *save = buttons->addButton("Save full report…", QDialogButtonBox::ActionRole);
    save->setObjectName("primary");
    buttons->addButton(QDialogButtonBox::Close);
    v->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &SweepDialog::run);
    connect(scope_, &QComboBox::currentIndexChanged, this, &SweepDialog::update_estimate);
    connect(max_toppings_, &QSpinBox::valueChanged, this, &SweepDialog::update_estimate);
    connect(max_proteins_, &QSpinBox::valueChanged, this, &SweepDialog::update_estimate);
    connect(sauces_, &QComboBox::currentIndexChanged, this, &SweepDialog::update_estimate);
    update_estimate();
    adjustSize();
}

SweepLimits SweepDialog::limits() const
{
    SweepLimits l;
    l.max_toppings = max_toppings_->value();
    l.max_proteins = max_proteins_->value();
    const QPoint cups = sauces_->currentData().toPoint();
    l.min_sauces = cups.x();
    l.max_sauces = cups.y();
    return l;
}

std::vector<const Menu *> SweepDialog::selected() const
{
    std::vector<const Menu *> out;
    const QString want = scope_->currentData().toString();
    for (const Menu &m : *ctx_.menus)
        if (want.isEmpty() || m.brand == want) out.push_back(&m);
    return out;
}

void SweepDialog::update_estimate()
{
    const SweepLimits l = limits();
    QLocale loc;
    qint64 rows = 0, bytes = 0;
    struct Line { QString brand, combos, recipes, rows; };
    std::vector<Line> table;
    for (const Menu *m : selected()) {
        const SweepPlan p = plan_sweep(*m, l);
        rows += p.rows();
        bytes += p.bytes();
        table.push_back({p.brand, loc.toString(p.combinations),
                         QString::number(p.recipes), loc.toString(p.rows())});
    }
    const QString total = loc.toString(rows);

    int w_brand = 5, w_combos = 6, w_recipes = 7, w_rows = std::max<int>(4, total.size());
    for (const Line &t : table) {
        w_brand = std::max<int>(w_brand, t.brand.size());
        w_combos = std::max<int>(w_combos, t.combos.size());
        w_recipes = std::max<int>(w_recipes, t.recipes.size());
        w_rows = std::max<int>(w_rows, t.rows.size());
    }

    QString text = QString("%1  %2  %3  %4\n")
                       .arg("", -w_brand)
                       .arg("combos", w_combos)
                       .arg("recipes", w_recipes)
                       .arg("bowls", w_rows);
    for (const Line &t : table)
        text += QString("%1  %2  %3  %4\n")
                    .arg(t.brand, -w_brand)
                    .arg(t.combos, w_combos)
                    .arg(t.recipes, w_recipes)
                    .arg(t.rows, w_rows);
    if (table.size() > 1)
        text += QString("%1  %2  %3  %4\n")
                    .arg("total", -w_brand)
                    .arg("", w_combos)
                    .arg("", w_recipes)
                    .arg(total, w_rows);
    text += QString("\n~%1 of CSV, about %2 to run.")
                .arg(human_bytes(bytes), human_time(rows / kRowsPerSecond));
    estimate_->setText(text);

    const theme::Palette &pal = theme::palette();
    auto set_warning = [this](const QString &text, const QColor &colour) {
        warning_->setText(text);
        warning_->setStyleSheet(QString("color:%1;").arg(colour.name()));
        fit_wrapped(warning_);
    };
    if (bytes > 2LL * 1024 * 1024 * 1024) {
        set_warning("That file is too large for Excel or Sheets to open. Lower the "
                    "topping cap unless you intend to query it with something else.",
                    pal.over);
    } else if (rows == 0) {
        set_warning("Nothing to sweep.", pal.ink_soft);
    } else {
        set_warning("Topping subsets are the term that explodes — each +1 on the cap "
                    "multiplies the row count again.",
                    pal.ink_faint);
    }
}

void SweepDialog::run()
{
    const std::vector<const Menu *> menus = selected();
    if (menus.empty()) return;

    const QString suggested =
        menus.size() == 1
            ? QString("%1-full-report.csv").arg(QString(menus.front()->brand).replace(' ', '-').toLower())
            : QString("all-brands-full-report.csv");
    const QString path =
        QFileDialog::getSaveFileName(this, "Save full report", suggested, "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Could not save", QString("Cannot write %1").arg(path));
        return;
    }
    QTextStream out(&file);

    QProgressDialog progress("Running the combinatorial workup…", "Cancel", 0, 1000, this);
    progress.setWindowTitle("Full combinatorial report");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();
    QLocale loc;
    std::vector<SweepTotals> totals;
    const bool finished = run_sweep(
        menus, *ctx_.curves, ctx_.method, ctx_.legacy, ctx_.adaptive, ctx_.costs, limits(),
        out, [&](qint64 done, qint64 total) {
            progress.setLabelText(QString("%1 of %2 bowls…")
                                      .arg(loc.toString(done), loc.toString(total)));
            progress.setValue(total > 0 ? int(done * 1000 / total) : 1000);
            return !progress.wasCanceled();
        },
        &totals);
    out.flush();
    file.close();
    progress.close();

    if (!finished) {
        QMessageBox::information(
            this, "Cancelled",
            QString("Stopped early. The partial report was still written to %1.").arg(path));
        return;
    }

    SweepTotals all;
    for (const SweepTotals &t : totals) {
        all.rows += t.rows;
        all.legacy_fits += t.legacy_fits;
        all.legacy_over += t.legacy_over;
        all.legacy_short += t.legacy_short;
        all.adaptive_over += t.adaptive_over;
        all.adaptive_over_bowl += t.adaptive_over_bowl;
        all.adaptive_short += t.adaptive_short;
    }
    QString summary =
        QString("%1 bowls in %2.\n\nUnder the legacy ramp: %3 come out right, %4 go over "
                "the bowl, %5 never reach their weight floor.\n\nUnder Adaptive: %6 still "
                "do not fit. A further %7 fit but cannot be brought down to the fill "
                "target, and %8 cannot reach it from below.\n\n")
            .arg(loc.toString(all.rows), human_time(timer.elapsed() / 1000.0))
            .arg(loc.toString(all.legacy_fits), loc.toString(all.legacy_over),
                 loc.toString(all.legacy_short))
            .arg(loc.toString(all.adaptive_over_bowl),
                 loc.toString(std::max<qint64>(0, all.adaptive_over - all.adaptive_over_bowl)),
                 loc.toString(all.adaptive_short));
    for (size_t i = 0; i < totals.size() && i < menus.size(); ++i)
        summary += QString("%1  %2% of bowls come out right\n")
                       .arg(menus[i]->brand, -20)
                       .arg(std::round(totals[i].legacy_pass_rate() * 100));
    summary += QString("\nWritten to %1.").arg(path);

    QMessageBox box(this);
    box.setWindowTitle("Full report saved");
    box.setText(summary);
    box.setIcon(QMessageBox::Information);
    box.exec();
}

}  // namespace bowlfill
