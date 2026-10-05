#pragma once

#include "core/simulator.hh"

#include <QWidget>

namespace bowlfill {

///
/// The final bowl in cross-section: a tapered vessel in real bowl proportions, each
/// ingredient a band sized by its share of the volume. It carries no legend: the
/// breakdown table beside it is keyed by item_colours(). Anything past the rim is
/// heaped above it rather than growing the bowl, so two runs stay comparable by eye.
///
class BowlDiagram : public QWidget {
    Q_OBJECT

public:
    explicit BowlDiagram(QWidget *parent = nullptr);

    void set_result(const SimResult &result);

    /// The band colour of each item in the last result, by index; invalid for an
    /// item too small to draw.
    std::vector<QColor> item_colours() const;

    /// The height a bowl this wide needs, footer included. Deliberately not reported
    /// through hasHeightForWidth(): the diagram sits in a fixed-width column, and a
    /// height-for-width child makes a stacked layout reserve room for a wider bowl
    /// than the one drawn. Callers fix the height from this instead.
    int heightForWidth(int w) const override;
    QSize minimumSizeHint() const override { return {260, heightForWidth(260)}; }
    QSize sizeHint() const override { return {380, heightForWidth(380)}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SimResult result_;
};

}  // namespace bowlfill
