#include "bowl_diagram.hh"
#include "theme.hh"
#include "units.hh"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

// Proportions taken from the kraft bowls in photos/: distinctly wider than tall,
// with sides tapering in toward a smaller base.
constexpr double kHeightOverRim = 0.46;
constexpr double kBaseOverRim = 0.64;
constexpr double kRimEllipse = 0.13;   // rim depth as a fraction of rim width

// No room is held above the rim for a heap: a bowl that fits should not sit under a
// band of empty space. An overfull bowl shrinks to fit its heap in the same height.
constexpr double kMargin = 12;
constexpr double kFooterGap = 12;

/// Vertical space the vessel takes beyond its depth, as a share of that depth: half
/// the rim ellipse above the rim line and half the base ellipse below the base. Both
/// the layout check and heightForWidth() must use this same figure, or the drawing
/// runs past the widget whenever the bowl is overfull.
constexpr double kEllipseShare = kRimEllipse * (1.0 + kBaseOverRim) / (2.0 * kHeightOverRim);

///
/// Distinct fills for ingredients with no assigned series hue. Bases keep their
/// series colour so the diagram, the breakdown table and the curve chart agree;
/// everything else walks lightness around a per-kind anchor, because rotating hue
/// would collide with the base series.
///
QColor band_colour(const BowlItem &item, int index_within_kind)
{
    if (item.kind == Kind::Base) return theme::series_colour(item.name);
    const theme::Palette &pal = theme::palette();
    const QColor anchor = item.kind == Kind::Protein ? pal.mass : pal.accent;
    static const int steps[] = {100, 132, 76, 116, 88, 148};
    return anchor.lighter(steps[index_within_kind % 6]);
}

struct Slice {
    QString name;
    QColor colour;
    double ounces = 0.0;
    double y_top = 0.0, y_bottom = 0.0;   // filled in during layout
};

}  // namespace

BowlDiagram::BowlDiagram(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void BowlDiagram::set_result(const SimResult &result)
{
    result_ = result;
    update();
}

void BowlDiagram::paintEvent(QPaintEvent *)
{
    const theme::Palette &pal = theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), pal.panel);
    if (result_.frames.empty()) return;

    const double cap = result_.settings.food_capacity_oz();
    if (cap <= 0) return;
    const double total = result_.last().ounces;
    const bool over = total > cap;

    // ---- slices, in dispense order so the stack matches how the bowl is built ----
    std::vector<Slice> slices;
    const std::vector<QColor> colours = item_colours();
    for (size_t i = 0; i < result_.items.size(); ++i) {
        const double oz = result_.last().per_item_oz[i];
        if (oz <= 0.005) continue;
        slices.push_back({result_.items[i].name, colours[i], oz, 0, 0});
    }
    if (slices.empty()) return;

    // ---- geometry ---------------------------------------------------------
    // The bowl fills the widget's width; the footer sits directly under it and the
    // whole block is centred vertically, so no space is stranded at the bottom.
    const QFont foot_font = theme::font(theme::Text::Heading, true);
    const QFont foot_note_font = theme::font(theme::Text::Caption);
    const double foot_line = QFontMetricsF(foot_font).height();
    const double footer_h = kFooterGap + foot_line + 2 + QFontMetricsF(foot_note_font).height();

    // How far past the rim the food heaps, as a multiple of the bowl's own depth.
    // Clamped for drawing so a wildly overfull bowl cannot squash the vessel itself
    // to nothing; the caption always states the true overage.
    const double over_ratio = std::max(0.0, total / cap - 1.0);
    const double drawn_over = std::min(over_ratio, 0.70);
    // The "+X over the rim" caption sits above the heap, so it needs a line too.
    const double caption_h = drawn_over > 0 ? QFontMetricsF(foot_note_font).height() + 6 : 0;
    const double reserve = drawn_over;

    const double avail_h = height() - footer_h - caption_h - kMargin * 2;
    double rim_w = width() - kMargin * 2;
    double bowl_h = rim_w * kHeightOverRim;
    if (bowl_h * (1.0 + reserve + kEllipseShare) > avail_h) {
        bowl_h = avail_h / (1.0 + reserve + kEllipseShare);
        rim_w = bowl_h / kHeightOverRim;
    }
    if (rim_w <= 40 || bowl_h <= 20) return;

    const double rim_hw = rim_w / 2, base_hw = rim_w * kBaseOverRim / 2;
    const double rim_ell = rim_w * kRimEllipse;
    const double base_ell = rim_ell * kBaseOverRim;

    const double block_h =
        caption_h + bowl_h * (1.0 + reserve) + rim_ell / 2 + base_ell / 2 + footer_h;
    const double top = std::max(kMargin, (height() - block_h) / 2) + caption_h;
    const double cx = width() / 2.0;
    const double rim_y = top + bowl_h * reserve + rim_ell / 2;
    const double base_y = rim_y + bowl_h;

    const double oz_per_px = cap / bowl_h;
    const double fill_px = total / oz_per_px;
    const double overflow_px = bowl_h * drawn_over;

    // ---- the bowl interior, and the heap above it -------------------------
    QPainterPath interior;
    interior.moveTo(cx - rim_hw, rim_y);
    interior.lineTo(cx - base_hw, base_y);
    interior.arcTo(QRectF(cx - base_hw, base_y - base_ell / 2, base_hw * 2, base_ell),
                   180, -180);
    interior.lineTo(cx + rim_hw, rim_y);
    interior.closeSubpath();

    QPainterPath fillable = interior;
    if (overflow_px > 0) {
        // A parabolic heap: the control point sits twice as high as the apex.
        QPainterPath heap;
        heap.moveTo(cx - rim_hw, rim_y);
        heap.quadTo(cx, rim_y - overflow_px * 2, cx + rim_hw, rim_y);
        heap.closeSubpath();
        fillable = fillable.united(heap);
    }

    // Inside of the far wall, so the bowl reads as a vessel rather than a bar.
    p.setPen(Qt::NoPen);
    p.setBrush(pal.panel_sunk);
    p.drawPath(interior);
    p.drawEllipse(QRectF(cx - rim_hw, rim_y - rim_ell / 2, rim_w, rim_ell));
    if (!over) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pal.rule_strong, 1.4));
        p.drawArc(QRectF(cx - rim_hw, rim_y - rim_ell / 2, rim_w, rim_ell),
                  180 * 16, 180 * 16);
    }

    // ---- stack the slices bottom-up, clipped to the bowl ------------------
    p.save();
    p.setClipPath(fillable);
    double y = base_y;
    for (Slice &s : slices) {
        const double h = s.ounces / oz_per_px;
        s.y_bottom = y;
        s.y_top = y - h;
        p.setBrush(s.colour);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(cx - rim_hw - 4, s.y_top, rim_w + 8, h + 0.5));
        y -= h;
    }
    p.restore();

    const double surface_y = base_y - fill_px;

    // The visible top surface of the food, only while it is still inside the bowl.
    if (overflow_px <= 0 && !slices.empty()) {
        const double t = (base_y - surface_y) / bowl_h;
        const double hw = base_hw + (rim_hw - base_hw) * std::clamp(t, 0.0, 1.0);
        const double ell = base_ell + (rim_ell - base_ell) * std::clamp(t, 0.0, 1.0);
        QColor top = slices.back().colour.lighter(112);
        p.setBrush(top);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(cx - hw, surface_y - ell / 2, hw * 2, ell));
    }

    // ---- bowl outline, over the fill --------------------------------------
    // Only the near half of the rim is drawn here. The far half was drawn before the
    // food and is legitimately hidden behind it, which is what stops the rim reading
    // as a lens across the middle of the bowl.
    const QRectF rim_rect(cx - rim_hw, rim_y - rim_ell / 2, rim_w, rim_ell);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pal.rule_strong, 1.8));
    QPainterPath wall;
    wall.moveTo(cx - rim_hw, rim_y);
    wall.lineTo(cx - base_hw, base_y);
    wall.arcTo(QRectF(cx - base_hw, base_y - base_ell / 2, base_hw * 2, base_ell),
               180, -180);
    wall.lineTo(cx + rim_hw, rim_y);
    p.drawPath(wall);
    p.drawArc(rim_rect, 180 * 16, -180 * 16);

    if (over) {
        p.setPen(QPen(pal.over, 1.6));
        QPainterPath heap;
        heap.moveTo(cx - rim_hw, rim_y);
        heap.quadTo(cx, rim_y - overflow_px * 2, cx + rim_hw, rim_y);
        p.setBrush(Qt::NoBrush);
        p.drawPath(heap);

        p.setPen(QPen(pal.over, 1.2, Qt::DashLine));
        p.drawLine(QPointF(cx - rim_hw - 14, rim_y), QPointF(cx + rim_hw + 14, rim_y));
        p.setFont(theme::font(theme::Text::Caption, true));
        p.setPen(pal.over);
        const double over_h = QFontMetricsF(p.font()).height();
        p.drawText(QRectF(cx - rim_hw, rim_y - overflow_px - over_h - 6, rim_w, over_h),
                   Qt::AlignCenter,
                   QString("+%1 over the rim").arg(units::volume(total - cap, true)));
    }


    // ---- footer, directly under the bowl ----------------------------------
    const double foot_y = base_y + base_ell / 2 + kFooterGap;
    p.setFont(foot_font);
    p.setPen(over ? pal.over : pal.ink);
    p.drawText(QRectF(0, foot_y, width(), foot_line), Qt::AlignCenter,
               result_.settings.geometry.enabled
                   // By height, cap is the room left for food, not the bowl.
                   ? QString("%1 of %2 food room · %3%")
                         .arg(units::volume(total, true), units::volume(cap, true))
                         .arg(std::round(total / cap * 100))
                   : QString("%1 of a %2 bowl · %3%")
                         .arg(units::volume(total, true), units::volume(cap, true))
                         .arg(std::round(total / cap * 100)));
    p.setFont(foot_note_font);
    p.setPen(pal.ink_faint);
    p.drawText(QRectF(0, foot_y + foot_line + 2, width(),
                      QFontMetricsF(foot_note_font).height()),
               Qt::AlignCenter,
               result_.settings.geometry.enabled
                   ? QString("%1 g · %2 bowl, %3 to cups and clearance")
                         .arg(std::round(result_.last().grams))
                         .arg(units::volume(result_.settings.bowl_capacity_oz, true),
                              units::volume(result_.settings.overhead_oz(), true))
                   : QString("%1 g total across %2 ingredients")
                         .arg(std::round(result_.last().grams))
                         .arg(slices.size()));
}

std::vector<QColor> BowlDiagram::item_colours() const
{
    std::vector<QColor> out(result_.items.size());
    if (result_.frames.empty()) return out;
    int base_i = 0, protein_i = 0, topping_i = 0;
    for (size_t i = 0; i < result_.items.size(); ++i) {
        if (result_.last().per_item_oz[i] <= 0.005) continue;
        const BowlItem &it = result_.items[i];
        int &counter = it.kind == Kind::Base      ? base_i
                       : it.kind == Kind::Protein ? protein_i
                                                  : topping_i;
        out[i] = band_colour(it, counter++);
    }
    return out;
}

int BowlDiagram::heightForWidth(int w) const
{
    const double rim_w = w - kMargin * 2;
    const double bowl_h = rim_w * kHeightOverRim;
    const double footer = kFooterGap + QFontMetricsF(theme::font(theme::Text::Heading, true)).height()
                          + 2 + QFontMetricsF(theme::font(theme::Text::Caption)).height();
    return static_cast<int>(std::ceil(kMargin * 2 + bowl_h * (1.0 + kEllipseShare) + footer));
}

}  // namespace bowlfill
