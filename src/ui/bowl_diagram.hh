#pragma once

#include "core/simulator.hh"

#include <QWidget>

namespace bowlfill {

///
/// The final bowl in cross-section: a tapered vessel in real bowl proportions, each
/// ingredient a band sized by its share of the volume, named in a column down the
/// side. Anything past the rim is heaped above it rather than growing the bowl, so
/// two runs stay comparable by eye.
///
class BowlDiagram : public QWidget {
    Q_OBJECT

public:
    explicit BowlDiagram(QWidget *parent = nullptr);

    void set_result(const SimResult &result);

    QSize minimumSizeHint() const override { return {430, 260}; }
    QSize sizeHint() const override { return {620, 320}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SimResult result_;
};

}  // namespace bowlfill
