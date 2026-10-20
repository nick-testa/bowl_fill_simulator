#pragma once

#include "core/curves.hh"

#include <QSet>
#include <QToolButton>
#include <QWidget>

class QMenu;

namespace bowlfill {

///
/// The measured mass-to-volume data and the power law fitted to it: one curve per
/// ingredient, solid over the measured span and dashed back to the origin so the
/// through-origin anchor is visible.
///
class CurveChart : public QWidget {
    Q_OBJECT

public:
    explicit CurveChart(QWidget *parent = nullptr);

    void set_curves(const CurveSet *curves);
    /// Ingredients left off the chart; the axes fit what is still shown.
    void set_hidden(const QSet<QString> &hidden);

    QSize minimumSizeHint() const override { return {420, 220}; }
    QSize sizeHint() const override { return {760, 280}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool shown(const QString &ingredient) const { return !hidden_.contains(ingredient); }
    /// The axis maxima for what is shown; false when there is nothing to plot.
    bool extent(double &g_max, double &oz_max) const;

    const CurveSet *curves_ = nullptr;
    QSet<QString> hidden_;
    QPointF hover_;
    bool hovering_ = false;
};

///
/// A drop-down of the chart's ingredients, each with a check box, for showing one
/// curve or a few at a time. The menu stays open while boxes are ticked. Everything
/// starts shown, and an ingredient that appears later (a new measurement file) is
/// shown too: what is remembered is what was hidden.
///
class CurveFilter : public QToolButton {
    Q_OBJECT

public:
    explicit CurveFilter(QWidget *parent = nullptr);

    /// The ingredients to offer, in chart order.
    void set_options(const QStringList &options);
    QSet<QString> hidden() const { return hidden_; }

signals:
    void changed(const QSet<QString> &hidden);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void relabel();

    QMenu *menu_ = nullptr;
    QStringList options_;
    QSet<QString> hidden_;
};

}  // namespace bowlfill
