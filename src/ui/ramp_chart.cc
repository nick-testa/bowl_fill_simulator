#include "ramp_chart.hh"
#include "theme.hh"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

constexpr int kLeft = 62, kRight = 22, kTop = 30, kBottom = 44;

/// Tick steps a reader expects: 1, 2, 2.5, 5 and their decades.
double nice_step(double span, int target)
{
    if (span <= 0) return 1.0;
    const double raw = span / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * mag >= raw) return m * mag;
    return mag * 10.0;
}

QList<double> span_ticks(double lo, double hi, int target)
{
    QList<double> out;
    const double step = nice_step(hi - lo, target);
    for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9; v += step) out << v;
    return out;
}

///
/// Floating callouts sit on top of dashed rules and each other, so each gets a
/// plate behind it.
///
void tag(QPainter &p, QPointF at, Qt::Alignment align, const QString &text,
         const QColor &colour, bool bold = true)
{
    QFont f = theme::mono(8, bold ? QFont::Bold : QFont::Normal);
    p.setFont(f);
    const QFontMetrics fm(f);
    QRectF box(0, 0, fm.horizontalAdvance(text) + 8, fm.height() + 3);
    box.moveTop(at.y() - box.height() / 2);
    if (align & Qt::AlignRight) box.moveRight(at.x());
    else if (align & Qt::AlignHCenter) box.moveLeft(at.x() - box.width() / 2);
    else box.moveLeft(at.x());

    QColor plate = theme::palette().panel;
    plate.setAlphaF(0.88f);
    p.setPen(Qt::NoPen);
    p.setBrush(plate);
    p.drawRoundedRect(box, 3, 3);
    p.setPen(colour);
    p.drawText(box, Qt::AlignCenter, text);
}

}  // namespace

RampChart::RampChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(300);
}

void RampChart::set_result(const SimResult &result)
{
    result_ = result;
    hover_ = -1;
    update();
}

void RampChart::set_show_uncompressed(bool show)
{
    show_uncompressed_ = show;
    update();
}

void RampChart::paintEvent(QPaintEvent *)
{
    const theme::Palette &pal = theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Antialiasing alone does not cover glyph rasterisation; without this the
    // chart labels are noticeably coarser than the rest of the window.
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), pal.panel);

    const auto &frames = result_.frames;
    if (frames.empty()) return;

    const double cap = result_.settings.bowl_capacity_oz;
    const double floor_g = result_.settings.floor_g;

    double g_min = frames.front().grams, g_max = frames.back().grams;
    for (const Frame &f : frames) {
        g_min = std::min(g_min, f.grams);
        g_max = std::max(g_max, f.grams);
    }
    if (floor_g > 0) g_max = std::max(g_max, floor_g);
    // A bowl that never ramps has a single point; give it a readable window rather
    // than a degenerate axis that prints the same tick five times.
    if (g_max - g_min < 1.0) {
        const double pad = std::max(10.0, g_max * 0.05);
        g_min -= pad;
        g_max += pad;
    } else {
        g_max += (g_max - g_min) * 0.05;
    }

    double oz_max = cap;
    for (const Frame &f : frames)
        oz_max = std::max(oz_max, show_uncompressed_
                                      ? std::max(f.ounces, f.ounces_uncompressed)
                                      : f.ounces);
    oz_max *= 1.14;

    const double pw = width() - kLeft - kRight;
    const double ph = height() - kTop - kBottom;
    if (pw <= 0 || ph <= 0) return;
    auto X = [&](double g) { return kLeft + (g - g_min) / (g_max - g_min) * pw; };
    auto Y = [&](double oz) { return kTop + ph - oz / oz_max * ph; };

    // The region above the bowl's capacity, tinted so overflow reads at a glance.
    const double cap_y = Y(cap);
    if (cap_y > kTop)
        p.fillRect(QRectF(kLeft, kTop, pw, cap_y - kTop), pal.over_soft);

    p.setFont(theme::mono(8));
    p.setPen(pal.rule);
    for (double v : span_ticks(0, oz_max, 5)) {
        const double y = Y(v);
        p.setPen(pal.rule);
        p.drawLine(QPointF(kLeft, y), QPointF(kLeft + pw, y));
        p.setPen(pal.ink_faint);
        p.drawText(QRectF(0, y - 8, kLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', (v > 0 && v < 10) ? 1 : 0));
    }
    for (double v : span_ticks(g_min, g_max, 5)) {
        p.setPen(pal.ink_faint);
        p.drawText(QRectF(X(v) - 30, kTop + ph + 4, 60, 14), Qt::AlignCenter,
                   QString::number(std::round(v)));
    }
    p.setPen(pal.rule_strong);
    p.drawLine(QPointF(kLeft, kTop + ph), QPointF(kLeft + pw, kTop + ph));

    // The uncompressed trace, when compression is on.
    if (show_uncompressed_) {
        QPainterPath ghost;
        ghost.moveTo(X(frames[0].grams), Y(frames[0].ounces_uncompressed));
        for (size_t i = 1; i < frames.size(); ++i) {
            ghost.lineTo(X(frames[i].grams), Y(frames[i - 1].ounces_uncompressed));
            ghost.lineTo(X(frames[i].grams), Y(frames[i].ounces_uncompressed));
        }
        QPen gp(pal.ink_faint, 1.4, Qt::DotLine);
        p.setPen(gp);
        p.setBrush(Qt::NoBrush);
        p.drawPath(ghost);
    }

    if (floor_g > 0 && floor_g >= g_min && floor_g <= g_max) {
        QPen fp(pal.mass, 2.0, Qt::DashLine);
        p.setPen(fp);
        p.drawLine(QPointF(X(floor_g), kTop), QPointF(X(floor_g), kTop + ph));
        const bool flip = X(floor_g) > kLeft + pw * 0.68;
        tag(p, QPointF(X(floor_g) + (flip ? -6 : 6), kTop - 12),
            flip ? Qt::AlignRight : Qt::AlignLeft,
            QString("weight floor %1 g").arg(std::round(floor_g)), pal.mass);
    }

    QPen cp(pal.over, 2.0, Qt::DashLine);
    p.setPen(cp);
    p.drawLine(QPointF(kLeft, cap_y), QPointF(kLeft + pw, cap_y));
    tag(p, QPointF(kLeft + pw, cap_y - 10), Qt::AlignRight,
        QString("volume limit %1 oz").arg(cap, 0, 'f', cap < 10 ? 1 : 0), pal.over);

    if (result_.highest_safe_floor_g) {
        const double g = *result_.highest_safe_floor_g;
        if (g >= g_min && g <= g_max) {
            QPen sp(pal.mass, 1.0, Qt::DotLine);
            p.setPen(sp);
            p.drawLine(QPointF(X(g), kTop), QPointF(X(g), kTop + ph));
            tag(p, QPointF(X(g) - 6, kTop + ph - 10), Qt::AlignRight,
                QString("safe floor %1 g").arg(std::round(g)), pal.mass, false);
        }
    }

    QPainterPath line;
    line.moveTo(X(frames[0].grams), Y(frames[0].ounces));
    for (size_t i = 1; i < frames.size(); ++i) {
        line.lineTo(X(frames[i].grams), Y(frames[i - 1].ounces));
        line.lineTo(X(frames[i].grams), Y(frames[i].ounces));
    }
    p.setPen(QPen(pal.accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);

    if (result_.crossed_at_g) {
        const double g = *result_.crossed_at_g;
        auto it = std::find_if(frames.begin(), frames.end(),
                               [&](const Frame &f) { return f.grams == g; });
        if (it != frames.end()) {
            p.setBrush(pal.over);
            p.setPen(QPen(pal.panel, 1.5));
            p.drawEllipse(QPointF(X(g), Y(it->ounces)), 4.5, 4.5);
            const bool near_left = X(g) - kLeft < 90;
            tag(p, QPointF(X(g) + (near_left ? 11 : -11), Y(it->ounces) + 16),
                near_left ? Qt::AlignLeft : Qt::AlignRight,
                QString("over at %1 g").arg(std::round(g)), pal.over);
        }
    }

    const Frame &last = frames.back();
    p.setBrush(pal.accent);
    p.setPen(QPen(pal.panel, 2));
    p.drawEllipse(QPointF(X(last.grams), Y(last.ounces)), 4.0, 4.0);

    if (hover_ >= 0 && hover_ < static_cast<int>(frames.size())) {
        const Frame &f = frames[hover_];
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pal.ink_faint, 1, Qt::DotLine));
        p.drawLine(QPointF(X(f.grams), kTop), QPointF(X(f.grams), kTop + ph));
        p.setBrush(pal.accent);
        p.setPen(QPen(pal.panel, 1.5));
        p.drawEllipse(QPointF(X(f.grams), Y(f.ounces)), 4.5, 4.5);
    }

    p.setFont(theme::mono(8));
    p.setPen(pal.ink_soft);
    p.drawText(QRectF(kLeft, height() - 16, pw, 14), Qt::AlignCenter,
               "total bowl mass (g)");
    p.save();
    p.translate(14, kTop + ph / 2);
    p.rotate(-90);
    p.drawText(QRectF(-100, -8, 200, 16), Qt::AlignCenter, "bowl volume (fl oz)");
    p.restore();
}

void RampChart::mouseMoveEvent(QMouseEvent *event)
{
    const auto &frames = result_.frames;
    if (frames.empty()) return;

    double g_min = frames.front().grams, g_max = frames.back().grams;
    for (const Frame &f : frames) {
        g_min = std::min(g_min, f.grams);
        g_max = std::max(g_max, f.grams);
    }
    if (result_.settings.floor_g > 0) g_max = std::max(g_max, result_.settings.floor_g);
    if (g_max - g_min < 1.0) {
        const double pad = std::max(10.0, g_max * 0.05);
        g_min -= pad;
        g_max += pad;
    } else {
        g_max += (g_max - g_min) * 0.05;
    }

    const double pw = width() - kLeft - kRight;
    if (pw <= 0) return;
    const double g = g_min + (event->position().x() - kLeft) / pw * (g_max - g_min);

    int best = -1;
    double best_d = 1e18;
    for (size_t i = 0; i < frames.size(); ++i) {
        const double d = std::fabs(frames[i].grams - g);
        if (d < best_d) { best_d = d; best = static_cast<int>(i); }
    }
    if (best != hover_) {
        hover_ = best;
        update();
    }
    if (best >= 0) {
        const Frame &f = frames[best];
        QToolTip::showText(event->globalPosition().toPoint(),
                           QString("pass %1\n%2 g\n%3 oz")
                               .arg(best)
                               .arg(f.grams, 0, 'f', 0)
                               .arg(f.ounces, 0, 'f', 1),
                           this);
    }
}

void RampChart::leaveEvent(QEvent *)
{
    hover_ = -1;
    QToolTip::hideText();
    update();
}

}  // namespace bowlfill
