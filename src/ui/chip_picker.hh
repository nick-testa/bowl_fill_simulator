#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QLayout;
class QPushButton;

namespace bowlfill {

///
/// Picks a few names from a fixed list: what is picked shows as chips, each with a
/// remove button, followed by an "+ Add" button whose menu offers only what is not
/// yet picked. The list is the brand's menu, so nothing outside it can be chosen, and
/// once `limit` names are picked the button is disabled.
///
class ChipPicker : public QWidget {
    Q_OBJECT

public:
    /// `noun` names one item in the add button and tooltips ("topping").
    explicit ChipPicker(const QString &noun, QWidget *parent = nullptr);

    /// What may be picked, in menu order. Picked names not in it are dropped.
    void set_options(const QStringList &options);
    /// The current picks; does not emit changed().
    void set_picked(const QStringList &picked);
    QStringList picked() const { return picked_; }
    /// At most this many picks (0 for no limit).
    void set_limit(int limit);
    /// Optional colour swatch for each name, shown on its chip.
    void set_swatches(std::function<QColor(const QString &)> swatch);

signals:
    void changed(const QStringList &picked);

private:
    void rebuild();

    QString noun_;
    QStringList options_, picked_;
    int limit_ = 0;
    std::function<QColor(const QString &)> swatch_;
    QLayout *flow_ = nullptr;
};

}  // namespace bowlfill
