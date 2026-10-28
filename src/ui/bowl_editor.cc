#include "bowl_editor.hh"

#include "theme.hh"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace bowlfill {

namespace {

/// How much of the bowl is shown: from the wall to this share of the rim's diameter.
constexpr double kCutShare = 0.25;
/// Below the floor and outside the wall, the bowl's own thickness, in mm.
constexpr double kShellMm = 2.0;
/// Room left above the lid for an overfilled bowl, in mm.
constexpr double kOverMm = 7.0;
/// The bowl, lid and all, takes this share of the panel's height (the golden section:
/// the bowl leads, with room around it), within bounds that keep it in proportion to
/// the text: never so small the layers are fiddly, never so large it dwarfs the labels.
constexpr double kHeightShare = 0.618;
constexpr double kMinPxPerMm = 4.5;
constexpr double kMaxPxPerMm = 10.0;
/// Where the spare height goes: a little more below than above sits the bowl at the
/// optical centre rather than the measured one.
constexpr double kSpareAbove = 0.4;

constexpr double kMargin = 8.0;
constexpr double kLabelGutter = 84.0;   ///< left of the wall: the level lines' labels
constexpr double kLeaderGap = 40.0;     ///< between the cut edge and the tabs
constexpr double kTabWidth = 236.0;
constexpr double kMinTabWidth = 180.0;   ///< tabs narrow to this before the bowl shrinks
constexpr double kTabHeight = 40.0;
constexpr double kTabGap = 8.0;
constexpr double kGrab = 7.0;           ///< how near an edge the pointer takes hold of it

}  // namespace

BowlEditor::BowlEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName("clear");
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BowlEditor::set_bowl(const BowlGeometry &geometry, const SauceCups &sauce, double room_oz,
                          double max_oz)
{
    geometry_ = geometry;
    sauce_ = sauce;
    room_oz_ = room_oz;
    max_oz_ = max_oz;
    update();
}

void BowlEditor::set_layers(std::vector<Layer> layers)
{
    layers_ = std::move(layers);
    if (drag_ >= static_cast<int>(layers_.size())) drag_ = -1;
    update();
}

double BowlEditor::surface_mm(double food_oz) const
{
    return food_surface_mm(sauce_, geometry_, food_oz);
}

double BowlEditor::oz_at_mm(double mm) const
{
    if (mm <= 0) return 0.0;
    double lo = 0.0, hi = 1.0;
    while (surface_mm(hi) < mm && hi < 1e4) hi *= 2.0;
    for (int i = 0; i < 50; ++i) {
        const double mid = (lo + hi) / 2.0;
        (surface_mm(mid) < mm ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

double BowlEditor::y_at_mm(const Frame &f, double mm) const
{
    return f.floor_y - mm * f.px_per_mm;
}

double BowlEditor::mm_at_y(const Frame &f, double y) const
{
    return (f.floor_y - y) / f.px_per_mm;
}

BowlEditor::Frame BowlEditor::frame() const
{
    Frame f;
    const BowlGeometry &g = geometry_;
    const double cut_mm = g.top_diameter_mm * kCutShare;
    const double top_mm = g.lid_mm() + kOverMm;
    // Size the cutaway, true to scale, by the panel's height; narrower than that, it
    // fits beside its labels and tabs instead. Then centre the lot.
    // In a narrow panel the tabs give up width first, so the bowl keeps its size.
    const double avail_w = width() - 2 * kMargin - kLabelGutter - kLeaderGap - kMinTabWidth;
    const double avail_h = height() - 2 * kMargin;
    const double fit = std::min(avail_w / (cut_mm + kShellMm), avail_h / (top_mm + kShellMm));
    const double wanted = std::clamp(avail_h * kHeightShare / (top_mm + kShellMm), kMinPxPerMm,
                                     kMaxPxPerMm);
    f.px_per_mm = std::max(1.0, std::min(wanted, fit));
    f.tab_w = std::clamp(avail_w + kMinTabWidth - (cut_mm + kShellMm) * f.px_per_mm, kMinTabWidth,
                         kTabWidth);
    const double block_w = kLabelGutter + (cut_mm + kShellMm) * f.px_per_mm + kLeaderGap + f.tab_w;
    const double left = std::max(kMargin, (width() - block_w) / 2.0);
    f.x0 = left + kLabelGutter + kShellMm * f.px_per_mm;
    f.cut_x = f.x0 + cut_mm * f.px_per_mm;
    const double block_h = (top_mm + kShellMm) * f.px_per_mm;
    f.floor_y = std::max(kMargin, (height() - block_h) * kSpareAbove) + top_mm * f.px_per_mm;
    f.top_y = y_at_mm(f, top_mm);
    f.tab_x = f.cut_x + kLeaderGap;

    // The top of each layer, and its tab as near to it as the tabs' spacing allows.
    double cum = 0.0;
    for (const Layer &l : layers_) {
        cum += std::max(0.0, l.oz);
        f.edge_y.push_back(std::max(f.top_y, y_at_mm(f, surface_mm(cum))));
    }
    const int n = static_cast<int>(layers_.size());
    const double pitch = kTabHeight + kTabGap;
    std::vector<double> centre(n);
    const double lowest = f.floor_y - kTabHeight / 2;
    for (int i = 0; i < n; ++i)
        centre[i] = std::min(i == 0 ? lowest : centre[i - 1] - pitch, f.edge_y[i]);
    // Pushed past the top: settle them back down from there.
    const double highest = kMargin + kTabHeight / 2;
    for (int i = n - 1; i >= 0; --i)
        centre[i] = std::max(centre[i], i == n - 1 ? highest : centre[i + 1] + pitch);
    for (int i = 0; i < n; ++i)
        f.tabs.emplace_back(f.tab_x, centre[i] - kTabHeight / 2, f.tab_w, kTabHeight);
    return f;
}

int BowlEditor::hit(const Frame &f, const QPointF &pos) const
{
    for (int i = static_cast<int>(f.tabs.size()) - 1; i >= 0; --i)
        if (f.tabs[i].contains(pos)) return i;
    if (pos.x() < f.x0 - kGrab || pos.x() > f.cut_x + kGrab) return -1;
    // The nearest edge; of edges that coincide, the top one, since dragging it up
    // brings the layers under it back into view.
    int best = -1;
    double best_d = kGrab;
    for (int i = 0; i < static_cast<int>(f.edge_y.size()); ++i) {
        const double d = std::fabs(f.edge_y[i] - pos.y());
        if (d <= best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void BowlEditor::paintEvent(QPaintEvent *)
{
    const theme::Palette &pal = theme::palette();
    const BowlGeometry &g = geometry_;
    if (g.depth_mm <= 0 || g.top_diameter_mm <= 0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Frame f = frame();
    const double ppm = f.px_per_mm;
    const double taper_mm = std::max(0.0, (g.top_diameter_mm - g.bottom_diameter_mm) / 2.0);
    auto x_at = [&](double mm) { return f.x0 + mm * ppm; };
    const double rim_y = y_at_mm(f, g.depth_mm);

    // ---- the inside of the bowl, carried straight up past the rim for any heap ----
    QPainterPath inside;
    inside.moveTo(f.x0, f.top_y);
    inside.lineTo(f.x0, rim_y);
    inside.lineTo(x_at(taper_mm), f.floor_y);
    inside.lineTo(f.cut_x, f.floor_y);
    inside.lineTo(f.cut_x, f.top_y);
    inside.closeSubpath();
    QPainterPath bowl_inside;
    bowl_inside.moveTo(f.x0, rim_y);
    bowl_inside.lineTo(x_at(taper_mm), f.floor_y);
    bowl_inside.lineTo(f.cut_x, f.floor_y);
    bowl_inside.lineTo(f.cut_x, rim_y);
    bowl_inside.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.panel_sunk);
    p.drawPath(bowl_inside);

    // ---- the food, a band per layer --------------------------------------------
    {
        p.save();
        p.setClipPath(inside);
        double below = f.floor_y;
        for (size_t i = 0; i < layers_.size(); ++i) {
            const double top = f.edge_y[i];
            if (below - top > 0.1) {
                p.setBrush(layers_[i].colour);
                p.drawRect(QRectF(f.x0 - 2, top, f.cut_x - f.x0 + 4, below - top));
            }
            below = top;
        }
        // Thin seams, so neighbours of a similar colour still read apart.
        p.setPen(QPen(pal.panel, 1.0));
        for (size_t i = 0; i + 1 < layers_.size(); ++i)
            p.drawLine(QPointF(f.x0 - 2, f.edge_y[i]), QPointF(f.cut_x, f.edge_y[i]));
        p.restore();
    }

    // ---- the bowl's wall and floor, in section --------------------------------
    {
        const double shell = kShellMm * ppm;
        QPainterPath wall;
        wall.moveTo(f.x0 - shell * 0.6, rim_y - 2);
        wall.lineTo(x_at(taper_mm) - shell * 0.8, f.floor_y + shell);
        wall.lineTo(f.cut_x, f.floor_y + shell);
        wall.lineTo(f.cut_x, f.floor_y);
        wall.lineTo(x_at(taper_mm), f.floor_y);
        wall.lineTo(f.x0, rim_y);
        wall.closeSubpath();
        // A light section with a crisp edge: the food, not the bowl, carries the weight.
        p.setPen(QPen(pal.rule_strong, 1.5));
        p.setBrush(pal.rule);
        p.drawPath(wall);
        // The rim's rolled lip.
        p.drawEllipse(QPointF(f.x0 - shell * 0.3, rim_y - 2), shell * 0.7, shell * 0.7);
    }

    // ---- the lid: flat at the rim, rising over its centre -------------------------
    if (g.lid_headroom_mm > 0) {
        const double step_mm =
            std::max(0.0, (g.top_diameter_mm - g.lid_plateau_diameter_mm) / 2.0);
        const double lid_y = y_at_mm(f, g.lid_mm());
        QPainterPath lid;
        lid.moveTo(f.x0 - kShellMm * ppm, rim_y - 3);
        lid.lineTo(x_at(step_mm), rim_y - 3);
        lid.lineTo(x_at(step_mm + 1.5), lid_y - 3);
        lid.lineTo(f.cut_x, lid_y - 3);
        p.setPen(QPen(pal.ink_faint, 1.4, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPath(lid);
    }

    // ---- the cut edge: where the rest of the bowl was taken away -----------------
    p.setPen(QPen(pal.ink_faint, 1.2, Qt::DashLine));
    p.drawLine(QPointF(f.cut_x, f.top_y), QPointF(f.cut_x, f.floor_y + kShellMm * ppm));

    // ---- level lines, labelled on the left ---------------------------------------
    const QFont label_font = theme::font(theme::Text::Body, true);
    p.setFont(label_font);
    auto level = [&](double oz, const QColor &colour, Qt::PenStyle style, const QString &text) {
        const double y = y_at_mm(f, surface_mm(oz));
        p.setPen(QPen(colour, 1.5, style));
        p.drawLine(QPointF(f.x0 - kShellMm * ppm - 6, y), QPointF(f.cut_x, y));
        p.drawText(QRectF(f.x0 - kShellMm * ppm - kLabelGutter, y - 11, kLabelGutter - 10, 22),
                   Qt::AlignRight | Qt::AlignVCenter, text);
    };
    if (room_oz_ > 0) {
        level(room_oz_, pal.ink, Qt::DotLine, "full");
        if (max_oz_ > 0 && std::fabs(max_oz_ - room_oz_) > 0.05)
            level(max_oz_, pal.accent, Qt::DashLine,
                  QString("max %1%").arg(std::round(max_oz_ / room_oz_ * 100)));
    }

    if (layers_.empty()) {
        p.setFont(theme::font(theme::Text::Body));
        p.setPen(pal.ink_soft);
        p.drawText(QRectF(f.x0, f.floor_y - 60, f.cut_x - f.x0, 40), Qt::AlignCenter | Qt::TextWordWrap,
                   "Add an ingredient to fill the bowl");
        return;
    }

    // ---- tabs, each tied to the top of its layer ---------------------------------
    double total = 0.0;
    for (const Layer &l : layers_) total += std::max(0.0, l.oz);
    const bool over_rim = surface_mm(total) > g.depth_mm + 0.05;
    for (size_t i = 0; i < layers_.size(); ++i) {
        const Layer &l = layers_[i];
        const bool active = static_cast<int>(i) == drag_ || static_cast<int>(i) == hover_;
        const QRectF &tab = f.tabs[i];
        const double ey = f.edge_y[i];
        // The edge itself, stronger while it is taken hold of.
        if (active) {
            p.setPen(QPen(pal.ink, 2.0));
            p.drawLine(QPointF(f.x0, ey), QPointF(f.cut_x, ey));
        }
        QPainterPath leader;
        leader.moveTo(f.cut_x, ey);
        leader.lineTo(f.cut_x + kLeaderGap * 0.35, ey);
        leader.lineTo(tab.left() - 4, tab.center().y());
        p.setPen(QPen(active ? pal.ink : pal.ink_faint, active ? 2.0 : 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(leader);
        p.setPen(QPen(pal.panel, 1.5));
        p.setBrush(l.colour);
        p.drawEllipse(QPointF(f.cut_x, ey), active ? 7.5 : 6.0, active ? 7.5 : 6.0);

        p.setPen(QPen(active ? l.colour : pal.rule_strong, active ? 2.0 : 1.0));
        p.setBrush(pal.panel);
        p.drawRoundedRect(tab, 8, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(l.colour);
        p.drawRoundedRect(QRectF(tab.left() + 12, tab.center().y() - 7, 14, 14), 4, 4);
        const QString pct = room_oz_ > 0 ? QString("%1%").arg(l.oz / room_oz_ * 100, 0, 'f', 1)
                                         : QString();
        // The share is what the chef is setting, so it is the tab's strongest text.
        p.setFont(theme::font(theme::Text::Heading, true));
        const QFontMetricsF pm(p.font());
        const double pct_w = pm.horizontalAdvance(pct) + 2;
        p.setPen(pal.ink);
        p.drawText(QRectF(tab.right() - pct_w - 12, tab.top(), pct_w, tab.height()),
                   Qt::AlignRight | Qt::AlignVCenter, pct);
        p.setFont(theme::font(theme::Text::Body));
        const QFontMetricsF nm(p.font());
        const QRectF name_rect(tab.left() + 34, tab.top(), tab.width() - 34 - pct_w - 20, tab.height());
        p.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                   nm.elidedText(l.name, Qt::ElideRight, name_rect.width()));
    }
    if (over_rim) {
        p.setFont(label_font);
        p.setPen(pal.over);
        p.drawText(QRectF(f.x0, f.top_y - 4, f.cut_x - f.x0, 22), Qt::AlignCenter,
                   "over the rim");
    }
}

void BowlEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    const Frame f = frame();
    drag_ = hit(f, event->position());
    if (drag_ < 0) return;
    drag_offset_ = f.edge_y[drag_] - event->position().y();
    update();
}

void BowlEditor::mouseMoveEvent(QMouseEvent *event)
{
    const Frame f = frame();
    if (drag_ < 0) {
        const int h = hit(f, event->position());
        if (h != hover_) {
            hover_ = h;
            setCursor(h >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
            setToolTip(h >= 0 ? QString("Drag to change how much %1 fills the bowl")
                                    .arg(layers_[h].name)
                              : QString());
            update();
        }
        return;
    }
    const double y = std::clamp(event->position().y() + drag_offset_, f.top_y, f.floor_y);
    emit edge_moved(drag_, oz_at_mm(mm_at_y(f, y)));
}

void BowlEditor::mouseReleaseEvent(QMouseEvent *)
{
    drag_ = -1;
    update();
}

void BowlEditor::leaveEvent(QEvent *)
{
    if (drag_ >= 0 || hover_ < 0) return;
    hover_ = -1;
    unsetCursor();
    update();
}

}  // namespace bowlfill
