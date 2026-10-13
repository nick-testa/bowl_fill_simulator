#include "chip_picker.hh"
#include "theme.hh"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <utility>

namespace bowlfill {
namespace {

///
/// Lays widgets out left to right, wrapping onto a new line when the row is full: the
/// chips are as wide as their names, so a grid would either waste room or clip them.
///
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int spacing) : QLayout(parent), gap_(spacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0)) delete item;
    }
    void addItem(QLayoutItem *item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem *itemAt(int i) const override { return items_.value(i); }
    QLayoutItem *takeAt(int i) override
    {
        return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : items_) size = size.expandedTo(item->minimumSize());
        return size;
    }

private:
    int arrange(const QRect &rect, bool measure_only) const
    {
        int x = rect.x(), y = rect.y(), line = 0;
        for (QLayoutItem *item : items_) {
            const QSize hint = item->sizeHint();
            if (x + hint.width() > rect.right() + 1 && line > 0) {
                x = rect.x();
                y += line + gap_;
                line = 0;
            }
            if (!measure_only) item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + gap_;
            line = std::max(line, hint.height());
        }
        return y + line - rect.y();
    }

    QList<QLayoutItem *> items_;
    int gap_;
};

}  // namespace

ChipPicker::ChipPicker(const QString &noun, QWidget *parent) : QWidget(parent), noun_(noun)
{
    setObjectName("clear");
    flow_ = new FlowLayout(this, theme::space::sm);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    rebuild();
}

void ChipPicker::set_options(const QStringList &options)
{
    options_ = options;
    QStringList kept;
    for (const QString &p : picked_)
        if (options_.contains(p)) kept << p;
    picked_ = kept;
    rebuild();
}

void ChipPicker::set_picked(const QStringList &picked)
{
    picked_ = picked;
    rebuild();
}

void ChipPicker::set_limit(int limit)
{
    limit_ = limit;
    rebuild();
}

void ChipPicker::set_write_in(bool allowed)
{
    write_in_ = allowed;
    rebuild();
}

void ChipPicker::set_swatches(std::function<QColor(const QString &)> swatch)
{
    swatch_ = std::move(swatch);
    rebuild();
}

void ChipPicker::rebuild()
{
    while (QLayoutItem *item = flow_->takeAt(0)) {
        // Hidden at once: deleteLater() leaves the old chip drawn until the event
        // loop gets round to it, on top of the new ones.
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    for (const QString &name : picked_) {
        auto *chip = new QWidget;
        chip->setObjectName("chip");
        chip->setAttribute(Qt::WA_StyledBackground);
        auto *h = new QHBoxLayout(chip);
        h->setContentsMargins(theme::space::sm, 2, 2, 2);
        h->setSpacing(theme::space::xs + 2);
        if (swatch_) {
            auto *dot = new QLabel;
            dot->setFixedSize(10, 10);
            dot->setStyleSheet(QString("background:%1;border-radius:3px;")
                                   .arg(swatch_(name).name()));
            h->addWidget(dot);
        }
        h->addWidget(new QLabel(name));
        auto *remove = new QToolButton;
        remove->setObjectName("chipRemove");
        remove->setText(QString::fromUtf8("✕"));
        remove->setToolTip(QString("Remove %1").arg(name));
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QToolButton::clicked, this, [this, name] {
            picked_.removeAll(name);
            rebuild();
            emit changed(picked_);
        });
        h->addWidget(remove);
        flow_->addWidget(chip);
    }

    QStringList available;
    for (const QString &o : options_)
        if (!picked_.contains(o)) available << o;
    auto *add = new QPushButton(QString("+ Add %1").arg(noun_));
    add->setObjectName("chipAdd");
    add->setCursor(Qt::PointingHandCursor);
    const bool full = limit_ > 0 && picked_.size() >= limit_;
    add->setEnabled((!available.isEmpty() || write_in_) && !full);
    add->setToolTip(options_.isEmpty() ? QString("This menu has no %1s.").arg(noun_)
                    : full ? QString("At most %1 %2%3.").arg(limit_).arg(noun_)
                                 .arg(limit_ == 1 ? "" : "s")
                    : available.isEmpty() ? QString("Every %1 on the menu is in.").arg(noun_)
                                          : QString("Choose from this menu's %1s.").arg(noun_));
    connect(add, &QPushButton::clicked, this, [this, add, available] {
        QMenu menu(this);
        for (const QString &name : available) {
            QAction *a = menu.addAction(name);
            connect(a, &QAction::triggered, this, [this, name] {
                picked_ << name;
                rebuild();
                emit changed(picked_);
            });
        }
        if (write_in_) {
            if (!available.isEmpty()) menu.addSeparator();
            QAction *other = menu.addAction(QString("Other %1…").arg(noun_));
            connect(other, &QAction::triggered, this, [this] {
                bool ok = false;
                const QString name =
                    QInputDialog::getText(this, QString("Add a %1").arg(noun_),
                                          QString("Name of the %1:").arg(noun_),
                                          QLineEdit::Normal, {}, &ok)
                        .trimmed();
                if (!ok || name.isEmpty() || picked_.contains(name)) return;
                if (!options_.contains(name)) options_ << name;
                picked_ << name;
                rebuild();
                emit changed(picked_);
            });
        }
        menu.exec(add->mapToGlobal(QPoint(0, add->height())));
    });
    flow_->addWidget(add);
    updateGeometry();
}

}  // namespace bowlfill
