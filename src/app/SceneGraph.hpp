#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSGGeometry>
#include <QSGMaterial>

#include <vector>

namespace qure::ui::sg {

/// Materials whose shaders need the current zoom (device pixels per canvas
/// unit) to anti-alias in screen space.
class ScaledMaterial : public QSGMaterial {
public:
    // RequiresFullMatrix: the shaders mix canvas-space and screen-space
    // quantities, so Qt must not pre-transform vertices when batching.
    ScaledMaterial() { setFlag(Blending | RequiresFullMatrix); }
    float scale = 1.0f;
    int compare(const QSGMaterial* other) const override;
};

class BoxMaterial final : public ScaledMaterial {
public:
    [[nodiscard]] QSGMaterialType* type() const override;
    [[nodiscard]] QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
};

class CordMaterial final : public ScaledMaterial {
public:
    [[nodiscard]] QSGMaterialType* type() const override;
    [[nodiscard]] QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
};

enum class Shape : int { Rect = 0, Message = 1, Broken = 2, Atom = 3 };

struct BoxVertex {
    float cx, cy;     // centre
    float kx, ky;     // corner sign
    float hw, hh;     // half size
    unsigned char fill[4];
    unsigned char stroke[4];
    float radius, border, shape, notch;
};
static_assert(sizeof(BoxVertex) == 48);

struct CordVertex {
    float x0, y0, x1, y1;
    float t, side;
    unsigned char color[4];
    float width;
};
static_assert(sizeof(CordVertex) == 32);

const QSGGeometry::AttributeSet& boxAttributes();
const QSGGeometry::AttributeSet& cordAttributes();

/// Accumulates quads, then fills a QSGGeometry in one go.
class BoxBatch {
public:
    void clear() { v_.clear(); }
    void add(const QRectF& r, const QColor& fill, const QColor& stroke, float borderPx, Shape shape = Shape::Rect,
             float radius = 2.0f, float notch = 4.0f);
    [[nodiscard]] std::size_t quads() const { return v_.size() / 4; }
    /// Returns a new geometry, or fills `g` if it already has the size.
    QSGGeometry* build(QSGGeometry* g) const;

private:
    std::vector<BoxVertex> v_;
};

class CordBatch {
public:
    void clear() { v_.clear(); }
    void add(QPointF a, QPointF b, const QColor& color, float width);
    [[nodiscard]] std::size_t quads() const { return v_.size() / 4; }
    QSGGeometry* build(QSGGeometry* g) const;

private:
    std::vector<CordVertex> v_;
};

} // namespace qure::ui::sg
