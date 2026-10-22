#include "WaveView.hpp"

#include "SceneGraph.hpp"

#include <QQuickWindow>
#include <QSGGeometryNode>

#include <algorithm>

namespace qure::ui {

namespace {
class WaveNode final : public QSGGeometryNode {
public:
    WaveNode() {
        setMaterial(&material);
        setFlag(OwnsGeometry);
    }
    sg::CordMaterial material;
    sg::CordBatch batch;
};
} // namespace

WaveView::WaveView(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void WaveView::setSamples(const QList<float>& s) {
    samples_ = s;
    emit samplesChanged();
    update();
}

void WaveView::setColor(const QColor& c) {
    if (c == color_)
        return;
    color_ = c;
    emit appearanceChanged();
    update();
}

void WaveView::setLineWidth(qreal w) {
    if (w == lineWidth_)
        return;
    lineWidth_ = w;
    emit appearanceChanged();
    update();
}

void WaveView::setMinimum(qreal v) {
    if (v == min_)
        return;
    min_ = v;
    emit appearanceChanged();
    update();
}

void WaveView::setMaximum(qreal v) {
    if (v == max_)
        return;
    max_ = v;
    emit appearanceChanged();
    update();
}

void WaveView::setPoints(bool p) {
    if (p == points_)
        return;
    points_ = p;
    emit appearanceChanged();
    update();
}

QSGNode* WaveView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = old ? static_cast<WaveNode*>(old) : new WaveNode;
    node->material.scale = static_cast<float>(window() ? window()->effectiveDevicePixelRatio() : 1.0);
    node->batch.clear();
    const auto n = samples_.size();
    const double w = width(), h = height();
    const double range = (max_ - min_) != 0 ? (max_ - min_) : 1.0;
    auto point = [&](qsizetype i) {
        const double x = n > 1 ? w * static_cast<double>(i) / static_cast<double>(n - 1) : w / 2;
        const double v = std::clamp((samples_[i] - min_) / range, 0.0, 1.0);
        return QPointF(x, h - v * h);
    };
    const auto lw = static_cast<float>(lineWidth_);
    if (points_) {
        for (qsizetype i = 0; i < n; ++i) {
            const QPointF p = point(i);
            node->batch.add(p - QPointF(lw, 0), p + QPointF(lw, 0), color_, lw);
        }
    } else {
        for (qsizetype i = 1; i < n; ++i)
            node->batch.add(point(i - 1), point(i), color_, lw);
    }
    QSGGeometry* g = node->batch.build(node->geometry());
    if (g != node->geometry())
        node->setGeometry(g);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

} // namespace qure::ui
