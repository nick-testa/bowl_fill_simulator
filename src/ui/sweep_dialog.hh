#pragma once

#include "core/sweep.hh"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace bowlfill {

/// Everything the sweep needs from the main window, so the dialog stays a view.
struct SweepContext {
    const std::vector<Menu> *menus = nullptr;
    QString brand;
    const CurveSet *curves = nullptr;
    Method method = Method::Robot;
    SimSettings legacy;
    AdaptiveSettings adaptive;
    const CostTable *costs = nullptr;
};

///
/// Sizes and runs the combinatorial workup. The estimate is live because the topping
/// cap moves the row count by orders of magnitude, and the difference between a 40 MB
/// file and a 20 GB one should be visible before the run starts, not after.
///
class SweepDialog : public QDialog {
    Q_OBJECT

public:
    SweepDialog(SweepContext context, QWidget *parent = nullptr);

private:
    void update_estimate();
    void run();
    SweepLimits limits() const;
    std::vector<const Menu *> selected() const;

    SweepContext ctx_;
    QComboBox *scope_ = nullptr;
    QComboBox *sauces_ = nullptr;
    QSpinBox *max_toppings_ = nullptr;
    QSpinBox *max_proteins_ = nullptr;
    QLabel *estimate_ = nullptr;
    QLabel *warning_ = nullptr;
};

}  // namespace bowlfill
