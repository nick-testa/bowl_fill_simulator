#include "tolerance_chart.hh"
#include "theme.hh"
#include "units.hh"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {
constexpr int kRowH = 26;
constexpr int kNameW = 168;
constexpr int kValueW = 132;
constexpr int kTop = 26, kBottom = 22;
}  // namespace

ToleranceChart::ToleranceChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(200);
}

void ToleranceChart::set_result(const AdaptiveResult &result)
{
    result_ = result;
    setMinimumHeight(kTop + kBottom + std::max<int>(3, result_.items.size()) * kRowH);
    update();
}

void ToleranceChart::paintEvent(QPaintEvent *)
{
    const theme::Palette &pal = theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), pal.panel);
    if (result_.items.empty()) return;

    const Tolerances &tol = result_.settings.tolerances;
    double widest = 0.05;
    for (const AdaptiveItem &it : result_.items)
        widest = std::max(widest, tol.for_kind(it.kind));
    widest *= 1.25;   // headroom either side of the widest band

    const double x0 = kNameW;
    const double w = width() - kNameW - kValueW;
    if (w <= 40) return;
    const double cx = x0 + w / 2;
    auto X = [&](double frac) { return cx + frac / widest * (w / 2); };

    // Axis: zero, and a tick at each configured tolerance edge.
    p.setFont(theme::mono(8));
    p.setPen(pal.rule);
    for (double t : {-widest, -tol.base, -tol.topping, -tol.protein, 0.0,
                     tol.protein, tol.topping, tol.base, widest}) {
        if (std::fabs(t) > widest) continue;
        p.setPen(std::fabs(t) < 1e-9 ? pal.rule_strong : pal.rule);
        p.drawLine(QPointF(X(t), kTop - 6), QPointF(X(t), height() - kBottom));
    }
    p.setPen(pal.ink_faint);
    for (double t : {-tol.base, 0.0, tol.base})
        p.drawText(QRectF(X(t) - 30, 4, 60, 14), Qt::AlignCenter,
                   t == 0 ? QString("nominal")
                          : QString("%1%2%").arg(t > 0 ? "+" : "").arg(t * 100, 0, 'f', 0));

    double y = kTop;
    for (const AdaptiveItem &it : result_.items) {
        const double t = tol.for_kind(it.kind);
        const QColor colour = it.kind == Kind::Base ? theme::series_colour(it.name)
                              : it.kind == Kind::Protein ? pal.mass
                                                         : pal.accent;

        p.setPen(pal.ink);
        p.setFont(theme::sans(9));
        const QFontMetricsF fm(p.font());
        p.drawText(QRectF(8, y, kNameW - 16, kRowH), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(it.name, Qt::ElideRight, kNameW - 20));

        // The band this ingredient is allowed to move within.
        QColor band = colour;
        band.setAlphaF(0.16f);
        p.setPen(Qt::NoPen);
        p.setBrush(band);
        p.drawRoundedRect(QRectF(X(-t), y + kRowH / 2.0 - 7, X(t) - X(-t), 14), 4, 4);

        // Where it actually landed.
        const double d = it.delta_pct() / 100.0;
        p.setBrush(colour);
        const double bar_l = std::min(X(0), X(d)), bar_r = std::max(X(0), X(d));
        p.drawRoundedRect(QRectF(bar_l, y + kRowH / 2.0 - 5, std::max(2.0, bar_r - bar_l), 10),
                          3, 3);
        p.setPen(QPen(pal.panel, 1.5));
        p.setBrush(colour);
        p.drawEllipse(QPointF(X(d), y + kRowH / 2.0), 4.5, 4.5);

        if (it.clamped) {
            p.setPen(QPen(pal.over, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(X(d), y + kRowH / 2.0), 7.0, 7.0);
        }

        p.setFont(theme::mono(8));
        p.setPen(std::fabs(it.delta_pct()) > 0.05 ? pal.ink : pal.ink_faint);
        p.drawText(QRectF(width() - kValueW, y, kValueW - 8, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1 g  %2%3%")
                       .arg(std::round(it.final_g))
                       .arg(it.delta_pct() > 0.05 ? "+" : "")
                       .arg(it.delta_pct(), 0, 'f', 1));
        y += kRowH;
    }

    p.setFont(theme::mono(8));
    p.setPen(pal.ink_faint);
    p.drawText(QRectF(0, height() - kBottom + 2, width(), 14), Qt::AlignCenter,
               QString("each ingredient moved %1% of its own band")
                   .arg(std::fabs(result_.alpha) * 100, 0, 'f', 0));
}

}  // namespace bowlfill
