#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace qure::ui {

/// Draws a list of samples as an anti-aliased polyline on the GPU. Meant for
/// node front ends: `WaveView { samples: node.samples }` is an oscilloscope.
class WaveView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<float> samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY appearanceChanged)
    Q_PROPERTY(qreal minimum READ minimum WRITE setMinimum NOTIFY appearanceChanged)
    Q_PROPERTY(qreal maximum READ maximum WRITE setMaximum NOTIFY appearanceChanged)
    Q_PROPERTY(bool points READ points WRITE setPoints NOTIFY appearanceChanged)

public:
    explicit WaveView(QQuickItem* parent = nullptr);

    [[nodiscard]] QList<float> samples() const { return samples_; }
    void setSamples(const QList<float>& s);
    [[nodiscard]] QColor color() const { return color_; }
    void setColor(const QColor& c);
    [[nodiscard]] qreal lineWidth() const { return lineWidth_; }
    void setLineWidth(qreal w);
    [[nodiscard]] qreal minimum() const { return min_; }
    void setMinimum(qreal v);
    [[nodiscard]] qreal maximum() const { return max_; }
    void setMaximum(qreal v);
    [[nodiscard]] bool points() const { return points_; }
    void setPoints(bool p);

signals:
    void samplesChanged();
    void appearanceChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    QList<float> samples_;
    QColor color_{Qt::black};
    qreal lineWidth_ = 1.5;
    qreal min_ = -1, max_ = 1;
    bool points_ = false;
};

} // namespace qure::ui
