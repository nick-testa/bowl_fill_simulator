#pragma once

#include "core/adaptive.hh"

#include <QWidget>

namespace bowlfill {

///
/// One row per ingredient: the band its tolerance allows, and where inside that band
/// the solution landed. Reading it top to bottom shows directly that protein barely
/// moves while base absorbs the adjustment, which is the tolerance table's whole
/// purpose and the thing a pass-by-pass chart cannot show.
///
class ToleranceChart : public QWidget {
    Q_OBJECT

public:
    explicit ToleranceChart(QWidget *parent = nullptr);

    void set_result(const AdaptiveResult &result);

    /// As tall as its rows: a two-ingredient bowl should not sit in a tall empty box.
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    AdaptiveResult result_;
};

}  // namespace bowlfill
