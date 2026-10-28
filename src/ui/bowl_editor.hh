#pragma once

#include "core/simulator.hh"

#include <QColor>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

namespace bowlfill {

///
/// The Bowl Builder's editor: the left quarter of the bowl in cross-section, drawn large
/// and true to scale (that much of the bowl is about as wide as it is deep), with each
/// ingredient a level band stacked in dispense order. Every band has a tab beyond the
/// cut edge, joined to the top of its band; dragging a tab, or the band's top edge
/// itself, moves that edge, so that ingredient grows or shrinks and everything above it
/// rides along. Tabs spread apart when their edges are close, so a thin band, or an
/// empty one, can still be taken hold of.
///
/// Heights come from volumes through the bowl's taper and the pressed-in cups, so a band
/// is exactly its ingredient's share of the bowl.
///
class BowlEditor : public QWidget {
    Q_OBJECT

public:
    struct Layer {
        QString name;
        QColor colour;
        double oz = 0.0;   ///< the ingredient's volume in the bowl
    };

    explicit BowlEditor(QWidget *parent = nullptr);

    /// The bowl and the lines drawn across it: the food room (how full the bowl may
    /// get) and the max the builder fills to, both in oz of food.
    void set_bowl(const BowlGeometry &geometry, const SauceCups &sauce, double room_oz,
                  double max_oz);
    void set_layers(std::vector<Layer> layers);

    QSize sizeHint() const override { return {620, 560}; }
    QSize minimumSizeHint() const override { return {420, 380}; }

signals:
    /// The top of layer `index` was dragged to where the food below it and the layer
    /// itself come to `top_oz`.
    void edge_moved(int index, double top_oz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// Where everything goes at the current size; recomputed for each paint and event.
    struct Frame {
        double px_per_mm = 1.0;
        double x0 = 0.0;           ///< the wall at the rim
        double cut_x = 0.0;        ///< the cut edge
        double floor_y = 0.0;      ///< the bowl's floor
        double top_y = 0.0;        ///< the highest food drawn
        double tab_x = 0.0, tab_w = 0.0;
        std::vector<double> edge_y;    ///< the top of each layer
        std::vector<QRectF> tabs;      ///< one per layer
    };
    Frame frame() const;
    double y_at_mm(const Frame &f, double mm) const;
    double mm_at_y(const Frame &f, double y) const;
    double surface_mm(double food_oz) const;
    double oz_at_mm(double mm) const;
    /// The layer whose tab or top edge is under `pos`, or -1.
    int hit(const Frame &f, const QPointF &pos) const;

    BowlGeometry geometry_;
    SauceCups sauce_;
    double room_oz_ = 0.0, max_oz_ = 0.0;
    std::vector<Layer> layers_;

    int drag_ = -1;                 ///< the layer being dragged
    double drag_offset_ = 0.0;      ///< from the pointer to that layer's edge, in px
    int hover_ = -1;
};

}  // namespace bowlfill
