#pragma once

#include "core/curves.hh"

#include <QWidget>

namespace bowlfill {

///
/// The measured mass-to-volume data and the power law fitted to it: one curve per
/// ingredient, solid over the measured span and dashed back to the origin so the
/// through-origin anchor is visible.
///
class CurveChart : public QWidget {
    Q_OBJECT

public:
    explicit CurveChart(QWidget *parent = nullptr);

    void set_curves(const CurveSet *curves, Method method);

    QSize minimumSizeHint() const override { return {420, 220}; }
    QSize sizeHint() const override { return {760, 280}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    const CurveSet *curves_ = nullptr;
    Method method_ = Method::Robot;
    QPointF hover_;
    bool hovering_ = false;
};

}  // namespace bowlfill
