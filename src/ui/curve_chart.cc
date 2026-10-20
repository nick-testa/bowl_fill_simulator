#include "curve_chart.hh"
#include "qt_text.hh"
#include "theme.hh"
#include "units.hh"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace bowlfill {
namespace {

constexpr int kLeft = 54, kRight = 104, kTop = 18, kBottom = 42;

double nice_step(double span, int target)
{
    if (span <= 0) return 1.0;
    const double raw = span / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * mag >= raw) return m * mag;
    return mag * 10.0;
}

QList<double> ticks(double hi, int target)
{
    QList<double> out;
    const double step = nice_step(hi, target);
    for (double v = 0; v <= hi + 1e-9; v += step) out << v;
    return out;
}

}  // namespace

CurveChart::CurveChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(220);
}

void CurveChart::set_curves(const CurveSet *curves)
{
    curves_ = curves;
    update();
}

void CurveChart::set_hidden(const QSet<QString> &hidden)
{
    hidden_ = hidden;
    update();
}

bool CurveChart::extent(double &g_max, double &oz_max) const
{
    g_max = oz_max = 0;
    for (const Observation &o : curves_->observations()) {
        if (!shown(qs(o.ingredient))) continue;
        g_max = std::max(g_max, o.grams);
        oz_max = std::max(oz_max, o.ounces);
    }
    if (g_max <= 0 || oz_max <= 0) return false;
    g_max *= 1.06;
    oz_max *= 1.14;
    return true;
}

void CurveChart::paintEvent(QPaintEvent *)
{
    const theme::Palette &pal = theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Antialiasing alone does not cover glyph rasterisation; without this the
    // chart labels are noticeably coarser than the rest of the window.
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), pal.panel);
    if (!curves_) return;

    QStringList names = qsl(curves_->ingredients());
    if (names.isEmpty()) return;
    names.removeIf([this](const QString &n) { return !shown(n); });

    double g_max = 0, oz_max = 0;
    if (!extent(g_max, oz_max)) {
        p.setFont(theme::font(theme::Text::Caption));
        p.setPen(pal.ink_faint);
        p.drawText(rect(), Qt::AlignCenter,
                   names.isEmpty() ? "No ingredients selected" : "Nothing measured this way");
        return;
    }

    const double pw = width() - kLeft - kRight;
    const double ph = height() - kTop - kBottom;
    if (pw <= 0 || ph <= 0) return;
    auto X = [&](double g) { return kLeft + g / g_max * pw; };
    auto Y = [&](double oz) { return kTop + ph - oz / oz_max * ph; };

    p.setFont(theme::font(theme::Text::Caption));
    for (double shown : ticks(units::from_oz(oz_max), 4)) {
        const double y = Y(units::to_oz(shown));
        p.setPen(pal.rule);
        p.drawLine(QPointF(kLeft, y), QPointF(kLeft + pw, y));
        p.setPen(pal.ink_faint);
        p.drawText(QRectF(0, y - 8, kLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(std::round(shown)));
    }
    for (double v : ticks(g_max, 5)) {
        p.setPen(pal.ink_faint);
        p.drawText(QRectF(X(v) - 30, kTop + ph + 4, 60, 14), Qt::AlignCenter,
                   QString::number(std::round(v)));
    }
    p.setPen(pal.rule_strong);
    p.drawLine(QPointF(kLeft, kTop + ph), QPointF(kLeft + pw, kTop + ph));

    // Label the rightmost-ending curve to its right; the others sit above their own
    // end, which keeps them off a neighbouring series.
    double widest = 0;
    for (const QString &n : names)
        if (const Fit *f = curves_->fit(ss(n))) widest = std::max(widest, f->hi_g);

    for (const QString &name : names) {
        const Fit *f = curves_->fit(ss(name));
        if (!f) continue;
        const QColor colour = theme::series_colour(name);

        QPainterPath head;   // dashed, back toward the origin
        head.moveTo(X(1), Y(f->volume_oz(1)));
        for (double g = 1; g <= f->lo_g; g += std::max((f->lo_g - 1) / 24.0, 0.5))
            head.lineTo(X(g), Y(f->volume_oz(g)));
        QColor faded = colour;
        faded.setAlphaF(0.55f);
        p.setPen(QPen(faded, 1.3, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPath(head);

        QPainterPath body;   // solid, over the measured span
        body.moveTo(X(f->lo_g), Y(f->volume_oz(f->lo_g)));
        const double stepg = std::max((f->hi_g - f->lo_g) / 40.0, 0.5);
        for (double g = f->lo_g; g <= f->hi_g; g += stepg)
            body.lineTo(X(g), Y(f->volume_oz(g)));
        body.lineTo(X(f->hi_g), Y(f->volume_oz(f->hi_g)));
        p.setPen(QPen(colour, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(body);

        QString label = name;
        label.remove(" Base");
        p.setFont(theme::font(theme::Text::Caption, true));
        p.setPen(colour);
        const QPointF end(X(f->hi_g), Y(f->volume_oz(f->hi_g)));
        if (f->hi_g >= widest - 1e-9)
            p.drawText(QRectF(end.x() + 6, end.y() - 8, kRight - 8, 16),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
        else
            p.drawText(QRectF(end.x() - 50, end.y() - 22, 100, 14), Qt::AlignCenter, label);

        p.setPen(QPen(pal.panel, 1.5));
        p.setBrush(colour);
        for (const Observation &o : curves_->observations()) {
            if (o.ingredient != name) continue;
            p.drawEllipse(QPointF(X(o.grams), Y(o.ounces)), 4.0, 4.0);
        }
    }

    p.setFont(theme::font(theme::Text::Caption));
    p.setPen(pal.ink_soft);
    p.drawText(QRectF(kLeft, height() - 16, pw, 14), Qt::AlignCenter,
               "ingredient mass (g)");
    p.save();
    p.translate(13, kTop + ph / 2);
    p.rotate(-90);
    p.drawText(QRectF(-100, -8, 200, 16), Qt::AlignCenter, units::curve_axis_label());
    p.restore();
}

void CurveChart::mouseMoveEvent(QMouseEvent *event)
{
    if (!curves_) return;
    double g_max = 0, oz_max = 0;
    if (!extent(g_max, oz_max)) return;

    const double pw = width() - kLeft - kRight;
    const double ph = height() - kTop - kBottom;
    if (pw <= 0 || ph <= 0) return;

    const Observation *best = nullptr;
    double best_d = 12.0;   // pixels
    for (const Observation &o : curves_->observations()) {
        if (!shown(qs(o.ingredient))) continue;
        const QPointF at(kLeft + o.grams / g_max * pw, kTop + ph - o.ounces / oz_max * ph);
        const double d = QLineF(at, event->position()).length();
        if (d < best_d) { best_d = d; best = &o; }
    }
    if (best)
        QToolTip::showText(event->globalPosition().toPoint(),
                           QString("%1\n%2 g → %3")
                               .arg(qs(best->ingredient))
                               .arg(best->grams, 0, 'f', 0)
                               .arg(units::volume(best->ounces, true)),
                           this);
    else
        QToolTip::hideText();
}

void CurveChart::leaveEvent(QEvent *) { QToolTip::hideText(); }

CurveFilter::CurveFilter(QWidget *parent) : QToolButton(parent)
{
    menu_ = new QMenu(this);
    menu_->installEventFilter(this);
    setMenu(menu_);
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    // The label carries its own arrow; Qt's would sit under it.
    setStyleSheet("QToolButton::menu-indicator { image: none; width: 0px; }");
    setToolTip("Choose which ingredients' curves the chart shows");
    relabel();
}

void CurveFilter::set_options(const QStringList &options)
{
    if (options == options_) return;
    options_ = options;
    rebuild();
    relabel();
}

void CurveFilter::rebuild()
{
    menu_->clear();
    auto set_all = [this](bool on) {
        hidden_ = on ? QSet<QString>{} : QSet<QString>(options_.begin(), options_.end());
        for (QAction *a : menu_->actions())
            if (a->isCheckable()) a->setChecked(on);
        relabel();
        emit changed(hidden_);
    };
    connect(menu_->addAction("Show all"), &QAction::triggered, this, [set_all] { set_all(true); });
    connect(menu_->addAction("Show none"), &QAction::triggered, this, [set_all] { set_all(false); });
    menu_->addSeparator();

    for (const QString &name : options_) {
        QPixmap dot(12, 12);
        dot.fill(Qt::transparent);
        {
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(theme::series_colour(name));
            p.drawEllipse(QRectF(1, 1, 10, 10));
        }
        QAction *a = menu_->addAction(QIcon(dot), name);
        a->setCheckable(true);
        a->setChecked(!hidden_.contains(name));
        connect(a, &QAction::toggled, this, [this, name](bool on) {
            if (on) hidden_.remove(name);
            else hidden_.insert(name);
            relabel();
            emit changed(hidden_);
        });
    }
}

void CurveFilter::relabel()
{
    int shown = 0;
    QString only;
    for (const QString &n : options_)
        if (!hidden_.contains(n)) { ++shown; only = n; }
    const int n = options_.size();
    QString text;
    if (shown == n) text = QString("All ingredients (%1)").arg(n);
    else if (shown == 0) text = "No ingredients";
    else if (shown == 1) text = only;
    else text = QString("%1 of %2 ingredients").arg(shown).arg(n);
    setText(text + "  ▾");
}

bool CurveFilter::eventFilter(QObject *watched, QEvent *event)
{
    // A click in the menu ticks the box and leaves the menu open, so several can be
    // picked in one go; Esc or a click outside closes it.
    if (watched == menu_ && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (QAction *a = menu_->actionAt(me->position().toPoint()); a && a->isEnabled()
            && !a->isSeparator()) {
            a->trigger();
            return true;
        }
    }
    return QToolButton::eventFilter(watched, event);
}

}  // namespace bowlfill
