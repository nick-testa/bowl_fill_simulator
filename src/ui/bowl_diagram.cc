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
constexpr int kRowHeight = 22;
constexpr int kLabelWidth = 216;

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
    setMinimumSize(430, 260);
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

    const double cap = result_.settings.bowl_capacity_oz;
    if (cap <= 0) return;
    const double total = result_.last().ounces;
    const bool over = total > cap;

    // ---- slices, in dispense order so the stack matches how the bowl is built ----
    std::vector<Slice> slices;
    int base_i = 0, protein_i = 0, topping_i = 0;
    for (size_t i = 0; i < result_.items.size(); ++i) {
        const double oz = result_.last().per_item_oz[i];
        if (oz <= 0.005) continue;
        const BowlItem &it = result_.items[i];
        int &counter = it.kind == Kind::Base      ? base_i
                       : it.kind == Kind::Protein ? protein_i
                                                  : topping_i;
        slices.push_back({it.name, band_colour(it, counter++), oz, 0, 0});
    }
    if (slices.empty()) return;

    // ---- geometry ---------------------------------------------------------
    const double margin = 14;
    const double footer_h = 34;
    const bool room_for_labels = width() > 470;
    const double label_x = width() - margin - kLabelWidth;
    const double bowl_area_w =
        (room_for_labels ? label_x - margin * 2 : width() - margin * 2);
    const double bowl_area_h = height() - footer_h - margin * 2;
    if (bowl_area_w <= 40 || bowl_area_h <= 40) return;

    // How far past the rim the food heaps, as a multiple of the bowl's own depth.
    // Clamped for drawing so a wildly overfull bowl cannot squash the vessel itself
    // to nothing; the caption always states the true overage.
    const double over_ratio = std::max(0.0, total / cap - 1.0);
    const double drawn_over = std::min(over_ratio, 0.70);

    // Width first, then shrink if the bowl plus its heap will not fit the height.
    double rim_w = std::min(bowl_area_w, 440.0);
    double bowl_h = rim_w * kHeightOverRim;
    const double ell_share = kRimEllipse * kHeightOverRim;   // rim ellipse, as a share
    if (bowl_h * (1.0 + drawn_over + ell_share) > bowl_area_h) {
        bowl_h = bowl_area_h / (1.0 + drawn_over + ell_share);
        rim_w = bowl_h / kHeightOverRim;
    }

    const double rim_hw = rim_w / 2, base_hw = rim_w * kBaseOverRim / 2;
    const double rim_ell = rim_w * kRimEllipse;
    const double base_ell = rim_ell * kBaseOverRim;

    const double cx = margin + bowl_area_w / 2;
    const double rim_y = margin + bowl_h * drawn_over + rim_ell / 2;
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
        p.setFont(theme::mono(8, QFont::Bold));
        p.setPen(pal.over);
        p.drawText(QRectF(cx - rim_hw, rim_y - overflow_px - 20, rim_w, 14),
                   Qt::AlignCenter,
                   QString("+%1 over the rim").arg(units::volume(total - cap, true)));
    }

    // ---- labels down the side ---------------------------------------------
    if (room_for_labels) {
        // More slices than rows would overlap, so the thinnest are pooled.
        const int max_rows = std::max(3, static_cast<int>(bowl_area_h / kRowHeight));
        // Listed in the order they appear down the bowl, so the column and the bands
        // can be read off against each other without leader lines.
        std::vector<Slice> shown(slices.rbegin(), slices.rend());
        if (static_cast<int>(shown.size()) > max_rows) {
            std::stable_sort(shown.begin(), shown.end(),
                             [](const Slice &a, const Slice &b) { return a.ounces > b.ounces; });
            Slice other{"Other", pal.ink_faint, 0, 0, 0};
            for (size_t i = max_rows - 1; i < shown.size(); ++i) other.ounces += shown[i].ounces;
            other.name = QString("Other (%1)").arg(shown.size() - max_rows + 1);
            shown.resize(max_rows - 1);
            shown.push_back(other);
        }

        const double block_h = shown.size() * kRowHeight;
        double ly = std::max<double>(margin, rim_y + bowl_h / 2 - block_h / 2);
        p.setFont(theme::sans(9));
        const QFontMetricsF fm(p.font());

        for (const Slice &s : shown) {
            const QRectF swatch(label_x, ly + kRowHeight / 2.0 - 5, 10, 10);
            p.setPen(Qt::NoPen);
            p.setBrush(s.colour);
            p.drawRoundedRect(swatch, 2.5, 2.5);

            const double text_x = label_x + 17;
            const double value_w = 94;
            p.setPen(pal.ink);
            p.drawText(QRectF(text_x, ly, kLabelWidth - 16 - value_w, kRowHeight),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(s.name, Qt::ElideRight,
                                     kLabelWidth - 17 - value_w - 8));
            p.setPen(pal.ink_soft);
            p.setFont(theme::mono(8));
            p.drawText(QRectF(label_x + kLabelWidth - value_w, ly, value_w, kRowHeight),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString("%1 · %2%")
                           .arg(units::volume(s.ounces, true))
                           .arg(std::round(s.ounces / total * 100)));
            p.setFont(theme::sans(9));
            ly += kRowHeight;
        }
    }

    // ---- footer -----------------------------------------------------------
    p.setFont(theme::mono(9, QFont::Bold));
    p.setPen(over ? pal.over : pal.ink);
    p.drawText(QRectF(0, height() - footer_h + 2, width(), 16), Qt::AlignCenter,
               QString("%1 of a %2 bowl · %3%")
                   .arg(units::volume(total, true), units::volume(cap, true))
                   .arg(std::round(total / cap * 100)));
    p.setFont(theme::mono(8));
    p.setPen(pal.ink_faint);
    p.drawText(QRectF(0, height() - footer_h + 18, width(), 14), Qt::AlignCenter,
               QString("%1 g total across %2 ingredients")
                   .arg(std::round(result_.last().grams))
                   .arg(slices.size()));
}

}  // namespace bowlfill
