#pragma once

#include "core/simulator.hh"

#include <QWidget>

namespace bowlfill {

///
/// Volume against total mass, one step per ramp pass, with the bowl's volume limit
/// and the recipe's weight floor drawn across it. Hovering a pass reports its mass
/// and volume.
///
class RampChart : public QWidget {
    Q_OBJECT

public:
    explicit RampChart(QWidget *parent = nullptr);

    void set_result(const SimResult &result);
    /// Draws the uncompressed trace alongside, so the squeeze is visible.
    void set_show_uncompressed(bool show);

    QSize minimumSizeHint() const override { return {420, 300}; }
    QSize sizeHint() const override { return {760, 380}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    SimResult result_;
    bool show_uncompressed_ = false;
    int hover_ = -1;
};

}  // namespace bowlfill
