#pragma once

#include "core/simulator.hh"

#include <QWidget>

#include <optional>
#include <vector>

namespace bowlfill {

/// The colour of each item's band, by index: its food's colour, with neighbours that
/// would read as one pulled apart in lightness. Invalid for an item with no volume.
std::vector<QColor> band_colours(const std::vector<BowlItem> &items, const std::vector<double> &oz);

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
    /// Marks a level the food should stay under (food volume, in oz) with a line labelled
    /// "max", in place of the 80% range; nothing clears it and shows the range again.
    void set_limit(std::optional<double> limit_oz);

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
    std::optional<double> limit_oz_;
};

}  // namespace bowlfill
