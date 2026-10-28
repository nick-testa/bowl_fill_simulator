#include "bowl_diagram.hh"
#include "qt_text.hh"
#include "theme.hh"
#include "units.hh"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bowlfill {
namespace {

// The bowl is drawn to its measured proportions (BowlGeometry): width and depth from
// the rim and floor diameters and the depth, and every level placed at the height its
// volume really reaches through the taper. Only the perspective is a choice.
constexpr double kRimEllipse = 0.075;  // rim depth as a fraction of rim width: a low,
                                       // shallow bowl reads best seen from low down

// Room is held above the rim for the lid's raised centre, but not for a heap: a bowl
// that fits should not sit under a band of empty space. An overfull bowl shrinks to
// fit its heap in the same height.
constexpr double kMargin = 12;
constexpr double kFooterGap = 12;
constexpr double kMaxDrawnOver = 0.70;   // heap height, as a share of the depth

/// The bowl's shape as drawn: depth and floor width as shares of the rim width, and
/// the vertical space the vessel takes beyond its depth (half the rim ellipse above
/// the rim; the floor is flat) as a share of the depth. Both
/// the layout and heightForWidth() use these, or the drawing runs past the widget.
struct Proportions {
    double height_over_rim, base_over_rim, ellipse_share;
};

Proportions proportions(const BowlGeometry &g)
{
    Proportions out{0.254, 0.89, 0};   // the measured bowl, should a dimension be zero
    if (g.top_diameter_mm > 0 && g.depth_mm > 0) {
        out.height_over_rim = g.depth_mm / g.top_diameter_mm;
        out.base_over_rim = std::clamp(g.bottom_diameter_mm / g.top_diameter_mm, 0.1, 1.0);
    }
    out.ellipse_share = kRimEllipse / (2.0 * out.height_over_rim);
    return out;
}

/// Room either side of the bowl for the level markers: the fill line's label on the
/// left, the 80% bracket and its label on the right.
struct Gutters {
    double left, right;
};

Gutters gutters()
{
    const QFontMetricsF fm(theme::font(theme::Text::Caption, true));
    return {fm.horizontalAdvance("food room") + 10, 14 + fm.horizontalAdvance("80%") + 4};
}

/// Sauce, inside a cup: warm enough to read as food on either palette.
const QColor kSauce(200, 92, 60);

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

    const SimSettings &set = result_.settings;
    const BowlGeometry &g = set.geometry;
    const double cap = set.food_capacity_oz();
    if (cap <= 0 || g.depth_mm <= 0) return;
    const double total = result_.last().ounces;
    const bool over = total > cap;
    auto surface_mm = [&](double oz) { return food_surface_mm(set.sauce, g, oz); };
    const double food_mm = surface_mm(total);
    // Mounded, each ingredient piles at its own slope on the ones below it; surfaces[k]
    // is the bowl after k layers (mound_surfaces), surfaces[0] the empty bowl.
    const bool mound = g.mound;
    const std::vector<std::vector<double>> surfaces =
        mound ? mound_surfaces(g, mound_layers(result_.items, result_.last().per_item_oz, g))
              : std::vector<std::vector<double>>{};
    const double peak_mm = mound ? highest_food_mm(surfaces) : food_mm;
    const double top_mm = std::max(food_mm, peak_mm);
    const double rim_oz = set.rim_oz();
    const bool over_rim = total > rim_oz;

    // ---- slices, in dispense order so the stack matches how the bowl is built ----
    std::vector<Slice> slices;
    const std::vector<QColor> colours = item_colours();
    for (size_t i = 0; i < result_.items.size(); ++i) {
        const double oz = result_.last().per_item_oz[i];
        if (oz <= 0.005) continue;
        slices.push_back({qs(result_.items[i].name), colours[i], oz, 0, 0});
    }
    if (slices.empty()) return;

    // ---- geometry ---------------------------------------------------------
    // The bowl fills the widget's width; the footer sits directly under it and the
    // whole block is centred vertically, so no space is stranded at the bottom.
    const QFont foot_font = theme::font(theme::Text::Heading, true);
    const QFont foot_note_font = theme::font(theme::Text::Caption);
    const double foot_line = QFontMetricsF(foot_font).height();
    const double footer_h = kFooterGap + foot_line + 2 + QFontMetricsF(foot_note_font).height();
    const Proportions shape = proportions(g);

    // Above the rim: the lid's raised centre always, and a heap when food passes the
    // lid's height, clamped so a wildly overfull bowl cannot squash the vessel to
    // nothing; the caption always states the true overage.
    const double lid_share = std::max(0.0, g.lid_headroom_mm) / g.depth_mm;
    const double drawn_over = std::min(std::max(0.0, top_mm - g.depth_mm) / g.depth_mm,
                                       kMaxDrawnOver);
    // The "+X over the rim" caption sits above the heap, so it needs a line too.
    const double caption_h = over_rim ? QFontMetricsF(foot_note_font).height() + 6 : 0;
    double reserve = std::max(lid_share, drawn_over);
    // A resting cup rides on the food and may stand well above the rim.
    if (!g.cups_pressed && set.sauce.cups > 0)
        reserve = std::max(reserve, (std::min(food_mm, g.lid_mm()) + g.cup_height_mm - g.depth_mm)
                                        / g.depth_mm);

    const double avail_h = height() - footer_h - caption_h - kMargin * 2;
    const Gutters gut = gutters();
    const double bowl_area = width() - kMargin * 2 - gut.left - gut.right;
    double rim_w = bowl_area;
    double bowl_h = rim_w * shape.height_over_rim;
    if (bowl_h * (1.0 + reserve + shape.ellipse_share) > avail_h) {
        bowl_h = avail_h / (1.0 + reserve + shape.ellipse_share);
        rim_w = bowl_h / shape.height_over_rim;
    }
    if (rim_w <= 40 || bowl_h <= 10) return;

    const double rim_hw = rim_w / 2, base_hw = rim_w * shape.base_over_rim / 2;
    const double rim_ell = rim_w * kRimEllipse;
    // The floor is flat, so it is drawn as a straight edge: no ellipse below it.
    const double base_ell = 0.0;

    const double block_h =
        caption_h + bowl_h * (1.0 + reserve) + rim_ell / 2 + base_ell / 2 + footer_h;
    const double top = std::max(kMargin, (height() - block_h) / 2) + caption_h;
    const double cx = kMargin + gut.left + bowl_area / 2.0;
    const double rim_y = top + bowl_h * reserve + rim_ell / 2;
    const double base_y = rim_y + bowl_h;

    const double px_per_mm = bowl_h / g.depth_mm;
    auto y_at_mm = [&](double mm) {
        return base_y - std::min(mm, g.depth_mm * (1.0 + kMaxDrawnOver)) * px_per_mm;
    };
    auto y_at_oz = [&](double oz) { return y_at_mm(surface_mm(oz)); };
    const double overflow_px = bowl_h * drawn_over;

    // ---- the bowl interior, and the heap above it -------------------------
    QPainterPath interior;
    interior.moveTo(cx - rim_hw, rim_y);
    interior.lineTo(cx - base_hw, base_y);
    interior.lineTo(cx + base_hw, base_y);
    interior.lineTo(cx + rim_hw, rim_y);
    interior.closeSubpath();

    // Over the rim, food first fills under the lid's raised centre, then heaps.
    const double plateau_hw =
        std::min(g.lid_plateau_diameter_mm, g.top_diameter_mm) / g.top_diameter_mm * rim_hw;
    const double lid_y = rim_y - std::max(0.0, g.lid_headroom_mm) * px_per_mm;
    QPainterPath fillable = interior;
    if (mound) {
        // A mound stands on the food below it, so above the rim it may be anywhere
        // within the rim's width; its own profile bounds it.
        fillable.addRect(QRectF(cx - rim_hw, 0, rim_w, rim_y));
    } else if (food_mm > g.depth_mm) {
        fillable.addRect(QRectF(cx - plateau_hw, lid_y, plateau_hw * 2, rim_y - lid_y + 1));
        if (food_mm > g.lid_mm()) {
            // A parabolic heap: the control point sits twice as high as the apex.
            QPainterPath heap;
            heap.moveTo(cx - rim_hw, rim_y);
            heap.quadTo(cx, rim_y - overflow_px * 2, cx + rim_hw, rim_y);
            heap.closeSubpath();
            fillable = fillable.united(heap);
        }
    }

    // Inside of the far wall, so the bowl reads as a vessel rather than a bar.
    p.setPen(Qt::NoPen);
    p.setBrush(pal.panel_sunk);
    p.drawPath(interior);
    p.drawEllipse(QRectF(cx - rim_hw, rim_y - rim_ell / 2, rim_w, rim_ell));
    if (!over_rim) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pal.rule_strong, 1.4));
        p.drawArc(QRectF(cx - rim_hw, rim_y - rim_ell / 2, rim_w, rim_ell),
                  180 * 16, 180 * 16);
    }

    // ---- stack the slices bottom-up, clipped to the bowl ------------------
    // Each band runs between the heights its cumulative volume reaches, so a band of
    // the same volume is thinner higher up, where the bowl is wider.
    p.save();
    p.setClipPath(fillable);
    double below_oz = 0;
    // A mounded layer: the band between the mound of everything below it and the
    // mound once it is added, sampled across the bowl.
    auto profile = [&](const std::vector<double> &surface, bool reverse) {
        constexpr int kSteps = 96;
        const double ring = g.top_diameter_mm / 2.0 / surface.size();
        QPolygonF line;
        for (int i = 0; i <= kSteps; ++i) {
            const int k = reverse ? kSteps - i : i;
            const double x = cx - rim_hw - 4 + (rim_w + 8) * k / kSteps;
            const size_t at = std::min(surface.size() - 1,
                                       static_cast<size_t>(std::fabs(x - cx) / px_per_mm / ring));
            line << QPointF(x, y_at_mm(surface[at]));
        }
        return line;
    };
    for (size_t k = 0; k < slices.size(); ++k) {
        Slice &s = slices[k];
        s.y_bottom = y_at_oz(below_oz);
        below_oz += s.ounces;
        s.y_top = y_at_oz(below_oz);
        p.setBrush(s.colour);
        p.setPen(Qt::NoPen);
        if (mound) {
            QPolygonF band = profile(surfaces[k + 1], false);
            band << profile(surfaces[k], true);
            p.drawPolygon(band);
        } else {
            p.drawRect(QRectF(cx - rim_hw - 4, s.y_top, rim_w + 8, s.y_bottom - s.y_top + 0.5));
        }
    }
    // A fine seam between layers, so neighbours of a similar colour still read apart.
    QColor seam = Qt::black;
    seam.setAlphaF(0.28);
    p.setPen(QPen(seam, 1.0));
    p.setBrush(Qt::NoBrush);
    for (size_t k = 0; k + 1 < slices.size(); ++k) {
        if (mound) p.drawPolyline(profile(surfaces[k + 1], false));
        else p.drawLine(QPointF(cx - rim_hw - 4, slices[k].y_top),
                        QPointF(cx + rim_hw + 4, slices[k].y_top));
    }
    p.restore();

    // The visible top surface of the food, only while it is still inside the bowl.
    if (!mound && food_mm <= g.depth_mm) {
        const double t = std::clamp(food_mm / g.depth_mm, 0.0, 1.0);
        const double hw = base_hw + (rim_hw - base_hw) * t;
        const double ell = base_ell + (rim_ell - base_ell) * t;
        const double surface_y = y_at_mm(food_mm);
        p.setBrush(slices.back().colour.lighter(112));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(cx - hw, surface_y - ell / 2, hw * 2, ell));
    }

    // Half-width of the bowl's inside at a height, for lines that start at its wall.
    auto half_width_at = [&](double y) {
        const double t = std::clamp((base_y - y) / bowl_h, 0.0, 1.0);
        return base_hw + (rim_hw - base_hw) * t;
    };
    const QFont label_font = theme::font(theme::Text::Caption, true);

    // ---- sauce cups, to scale --------------------------------------------
    // Pressed in, a cup's top is at the lid and the food closes round it; resting, it
    // sits on the food. Drawn opaque over the food, since that is what it displaces.
    const int cups = std::clamp(set.sauce.cups, 0, SauceCups::kMax);
    if (cups > 0 && g.cup_height_mm > 0) {
        const double mm = rim_w / g.top_diameter_mm;   // the drawing is to one scale
        const double top_hw = g.cup_diameter_mm * mm / 2, foot_hw = g.cup_base_diameter_mm * mm / 2;
        const double cup_h = g.cup_height_mm * mm;
        const double cup_top = g.cups_pressed ? y_at_mm(g.lid_mm())
                                              : y_at_mm(std::min(food_mm, g.lid_mm())) - cup_h;
        const double offsets[2][2] = {{0.35, 0}, {-0.42, 0.42}};
        for (int i = 0; i < cups; ++i) {
            const double x = cx + rim_hw * offsets[cups - 1][i];
            QPainterPath cup;
            cup.moveTo(x - top_hw, cup_top);
            cup.lineTo(x - foot_hw, cup_top + cup_h);
            cup.lineTo(x + foot_hw, cup_top + cup_h);
            cup.lineTo(x + top_hw, cup_top);
            cup.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(pal.panel);
            p.drawPath(cup);
            // The sauce, to about three quarters of the cup.
            p.save();
            p.setClipPath(cup);
            p.setBrush(kSauce);
            p.drawRect(QRectF(x - top_hw, cup_top + cup_h * 0.28, top_hw * 2, cup_h));
            p.restore();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(pal.ink_soft, 1.2));
            p.drawPath(cup);
            p.setPen(QPen(pal.ink_soft, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(x - top_hw - 2, cup_top), QPointF(x + top_hw + 2, cup_top));
        }
    }

    // ---- the food room: the fill line, or lower when cups or chunks need it ----
    // Carried out of the bowl to its label on the left, so it reads at a glance.
    bool room_is_fill_line = true;
    {
        const double room_mm = surface_mm(cap);
        const double fill_mm = g.height_of_mm3(set.bowl_capacity_oz * 29.5735295625 * 1000.0);
        room_is_fill_line = std::fabs(room_mm - fill_mm) < 0.05;
        const double y = y_at_mm(room_mm);
        const double left = kMargin + gut.left - 4;
        p.setPen(QPen(pal.ink, 1.4, Qt::DotLine));
        p.drawLine(QPointF(left, y), QPointF(cx + half_width_at(y) + 4, y));
        p.setFont(label_font);
        p.setPen(pal.ink);
        p.drawText(QRectF(kMargin - 4, y - 9, gut.left - 4, 18), Qt::AlignRight | Qt::AlignVCenter,
                   room_is_fill_line ? "fill line" : "food room");
    }

    // ---- the 80% range: where real bowls of this recipe land ------------------
    // A band between the 10th and 90th percentile fill levels, across the bowl. Above
    // the rim it is drawn rim-wide, which is close enough to say "this much over".
    // Its edges run out of the bowl to a bracket on the right, labelled.
    if (!limit_oz_ && result_.fill.valid && result_.fill.sd_oz > 0) {
        const double y_lo = y_at_oz(std::max(0.0, result_.fill.p10_oz));
        const double y_hi = std::max(kMargin, y_at_oz(result_.fill.p90_oz));
        if (y_lo > y_hi) {
            QPainterPath region = interior;
            region.addRect(QRectF(cx - rim_hw, kMargin, rim_w, rim_y - kMargin));
            p.save();
            p.setClipPath(region);
            QColor band = pal.ink;
            band.setAlphaF(0.12);
            p.setPen(Qt::NoPen);
            p.setBrush(band);
            p.drawRect(QRectF(cx - rim_hw - 4, y_hi, rim_w + 8, y_lo - y_hi));
            p.restore();

            const double bx = cx + rim_hw + 10;
            QPen edge(pal.ink_soft, 1.2, Qt::DashLine);
            p.setPen(edge);
            for (double y : {y_hi, y_lo})
                p.drawLine(QPointF(cx - half_width_at(y), y), QPointF(bx, y));
            p.setPen(QPen(pal.ink_soft, 1.6));
            p.drawLine(QPointF(bx, y_hi), QPointF(bx, y_lo));
            p.setFont(label_font);
            p.setPen(pal.ink_soft);
            const double mid = (y_hi + y_lo) / 2;
            p.drawText(QRectF(bx + 4, mid - 9, gut.right, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       "80%");
        }
    }

    // ---- the most the food may fill, where one is set: in the range's place ----
    if (limit_oz_) {
        const double y = y_at_oz(*limit_oz_);
        const double bx = cx + rim_hw + 10;
        p.setPen(QPen(pal.accent, 1.6, Qt::DashLine));
        p.drawLine(QPointF(cx - half_width_at(y), y), QPointF(bx, y));
        p.setFont(label_font);
        p.setPen(pal.accent);
        p.drawText(QRectF(bx + 4, y - 9, gut.right, 18), Qt::AlignLeft | Qt::AlignVCenter, "max");
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
    wall.lineTo(cx + base_hw, base_y);
    wall.lineTo(cx + rim_hw, rim_y);
    p.drawPath(wall);
    p.drawArc(rim_rect, 180 * 16, -180 * 16);

    // The lid in profile: flat at the rim, rising over its centre.
    if (g.lid_headroom_mm > 0) {
        QPainterPath lid;
        lid.moveTo(cx - rim_hw, rim_y);
        lid.lineTo(cx - plateau_hw, rim_y);
        lid.lineTo(cx - plateau_hw + 3, lid_y);
        lid.lineTo(cx + plateau_hw - 3, lid_y);
        lid.lineTo(cx + plateau_hw, rim_y);
        lid.lineTo(cx + rim_hw, rim_y);
        p.setPen(QPen(pal.ink_faint, 1.0, Qt::DashLine));
        p.drawPath(lid);
    }

    if (over_rim) {
        if (!mound && food_mm > g.lid_mm()) {
            p.setPen(QPen(pal.over, 1.6));
            QPainterPath heap;
            heap.moveTo(cx - rim_hw, rim_y);
            heap.quadTo(cx, rim_y - overflow_px * 2, cx + rim_hw, rim_y);
            p.setBrush(Qt::NoBrush);
            p.drawPath(heap);
        }
        p.setPen(QPen(pal.over, 1.2, Qt::DashLine));
        p.drawLine(QPointF(cx - rim_hw - 14, rim_y), QPointF(cx + rim_hw + 14, rim_y));
        p.setFont(theme::font(theme::Text::Caption, true));
        p.setPen(pal.over);
        const double over_h = QFontMetricsF(p.font()).height();
        const double peak = std::min(lid_y, mound ? y_at_mm(peak_mm) : rim_y - overflow_px);
        p.drawText(QRectF(cx - rim_hw, peak - over_h - 6, rim_w, over_h), Qt::AlignCenter,
                   QString("+%1 over the rim").arg(units::volume(total - rim_oz, true)));
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
    const QString note =
        result_.settings.geometry.enabled
            ? QString("%1 g · fill line %2 · %3 to cups and chunks")
                  .arg(std::round(result_.last().grams))
                  .arg(units::volume(result_.settings.bowl_capacity_oz, true),
                       units::volume(result_.settings.overhead_oz(), true))
            : QString("%1 g total across %2 ingredients")
                  .arg(std::round(result_.last().grams)).arg(slices.size());
    p.drawText(QRectF(0, foot_y + foot_line + 2, width(),
                      QFontMetricsF(foot_note_font).height()),
               Qt::AlignCenter,
               QFontMetricsF(foot_note_font).elidedText(note, Qt::ElideRight, width() - 4));
}

void BowlDiagram::set_limit(std::optional<double> limit_oz)
{
    limit_oz_ = limit_oz;
    update();
}

std::vector<QColor> band_colours(const std::vector<BowlItem> &items, const std::vector<double> &oz)
{
    std::vector<QColor> out(items.size());
    // Each band takes its food's colour. Two neighbours that would read as one (two
    // greens, two off-whites) are pulled apart in lightness, alternating so a run of
    // similar foods stays distinguishable.
    QColor previous;
    bool lifted = false;
    for (size_t i = 0; i < items.size() && i < oz.size(); ++i) {
        if (oz[i] <= 0.005) continue;
        const BowlItem &it = items[i];
        QColor c = theme::food_colour(qs(it.name), static_cast<int>(it.kind));
        if (previous.isValid()) {
            const double dr = c.redF() - previous.redF(), dg = c.greenF() - previous.greenF(),
                         db = c.blueF() - previous.blueF();
            if (std::sqrt(dr * dr + dg * dg + db * db) < 0.16) {
                lifted = !lifted;
                c = lifted ? c.lighter(122) : c.darker(122);
            }
        }
        out[i] = c;
        previous = c;
    }
    return out;
}

std::vector<QColor> BowlDiagram::item_colours() const
{
    if (result_.frames.empty()) return std::vector<QColor>(result_.items.size());
    return band_colours(result_.items, result_.last().per_item_oz);
}

int BowlDiagram::heightForWidth(int w) const
{
    const BowlGeometry &g = result_.settings.geometry;
    const Proportions shape = proportions(g);
    const Gutters gut = gutters();
    const double rim_w = w - kMargin * 2 - gut.left - gut.right;
    const double bowl_h = rim_w * shape.height_over_rim;
    const double lid_share = g.depth_mm > 0 ? std::max(0.0, g.lid_headroom_mm) / g.depth_mm : 0;
    const double footer = kFooterGap + QFontMetricsF(theme::font(theme::Text::Heading, true)).height()
                          + 2 + QFontMetricsF(theme::font(theme::Text::Caption)).height();
    return static_cast<int>(
        std::ceil(kMargin * 2 + bowl_h * (1.0 + lid_share + shape.ellipse_share) + footer));
}

}  // namespace bowlfill
