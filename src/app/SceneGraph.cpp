#include "SceneGraph.hpp"

#include <QSGMaterialShader>

#include <cstring>

namespace qure::ui::sg {
namespace {

class ScaledShader final : public QSGMaterialShader {
public:
    ScaledShader(const char* vertex, const char* fragment) {
        setShaderFileName(VertexStage, QString::fromLatin1(vertex));
        setShaderFileName(FragmentStage, QString::fromLatin1(fragment));
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override {
        QByteArray* buf = state.uniformData();
        Q_ASSERT(buf->size() >= 72);
        bool changed = false;
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
            changed = true;
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + 64, &opacity, 4);
            changed = true;
        }
        const auto* m = static_cast<ScaledMaterial*>(newMaterial);
        const auto* old = static_cast<ScaledMaterial*>(oldMaterial);
        if (!old || old->scale != m->scale || changed) {
            std::memcpy(buf->data() + 68, &m->scale, 4);
            changed = true;
        }
        return changed;
    }
};

void premultiplied(const QColor& c, unsigned char out[4]) {
    const float a = static_cast<float>(c.alphaF());
    out[0] = static_cast<unsigned char>(c.red() * a + 0.5f);
    out[1] = static_cast<unsigned char>(c.green() * a + 0.5f);
    out[2] = static_cast<unsigned char>(c.blue() * a + 0.5f);
    out[3] = static_cast<unsigned char>(c.alpha());
}

template <class Vertex>
QSGGeometry* fill(QSGGeometry* g, const QSGGeometry::AttributeSet& attrs, const std::vector<Vertex>& v) {
    const int vertices = static_cast<int>(v.size());
    const int indices = vertices / 4 * 6;
    if (!g)
        g = new QSGGeometry(attrs, vertices, indices, QSGGeometry::UnsignedIntType);
    else
        g->allocate(vertices, indices);
    g->setDrawingMode(QSGGeometry::DrawTriangles);
    if (vertices)
        std::memcpy(g->vertexData(), v.data(), v.size() * sizeof(Vertex));
    auto* idx = g->indexDataAsUInt();
    for (int q = 0; q < vertices / 4; ++q) {
        const auto b = static_cast<quint32>(q * 4);
        idx[q * 6 + 0] = b;
        idx[q * 6 + 1] = b + 1;
        idx[q * 6 + 2] = b + 2;
        idx[q * 6 + 3] = b + 2;
        idx[q * 6 + 4] = b + 1;
        idx[q * 6 + 5] = b + 3;
    }
    g->markIndexDataDirty();
    g->markVertexDataDirty();
    return g;
}

} // namespace

int ScaledMaterial::compare(const QSGMaterial* other) const {
    const auto* o = static_cast<const ScaledMaterial*>(other);
    if (scale == o->scale)
        return 0;
    return scale < o->scale ? -1 : 1;
}

QSGMaterialType* BoxMaterial::type() const {
    static QSGMaterialType t;
    return &t;
}

QSGMaterialShader* BoxMaterial::createShader(QSGRendererInterface::RenderMode) const {
    return new ScaledShader(":/qure/shaders/box.vert.qsb", ":/qure/shaders/box.frag.qsb");
}

QSGMaterialType* CordMaterial::type() const {
    static QSGMaterialType t;
    return &t;
}

QSGMaterialShader* CordMaterial::createShader(QSGRendererInterface::RenderMode) const {
    return new ScaledShader(":/qure/shaders/cord.vert.qsb", ":/qure/shaders/cord.frag.qsb");
}

const QSGGeometry::AttributeSet& boxAttributes() {
    using A = QSGGeometry::Attribute;
    static const A attrs[] = {
        A::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        A::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        A::createWithAttributeType(2, 2, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        A::createWithAttributeType(3, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
        A::createWithAttributeType(4, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
        A::createWithAttributeType(5, 4, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {6, sizeof(BoxVertex), attrs};
    return set;
}

const QSGGeometry::AttributeSet& cordAttributes() {
    using A = QSGGeometry::Attribute;
    static const A attrs[] = {
        A::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        A::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        A::createWithAttributeType(2, 2, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        A::createWithAttributeType(3, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
        A::createWithAttributeType(4, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {5, sizeof(CordVertex), attrs};
    return set;
}

void BoxBatch::add(const QRectF& r, const QColor& fillColor, const QColor& stroke, float borderPx, Shape shape,
                   float radius, float notch) {
    BoxVertex v{};
    v.cx = static_cast<float>(r.center().x());
    v.cy = static_cast<float>(r.center().y());
    v.hw = static_cast<float>(r.width() / 2);
    v.hh = static_cast<float>(r.height() / 2);
    premultiplied(fillColor, v.fill);
    premultiplied(stroke, v.stroke);
    v.radius = radius;
    v.border = borderPx;
    v.shape = static_cast<float>(shape);
    v.notch = notch;
    for (int i = 0; i < 4; ++i) {
        v.kx = (i & 1) ? 1.0f : -1.0f;
        v.ky = (i & 2) ? 1.0f : -1.0f;
        v_.push_back(v);
    }
}

QSGGeometry* BoxBatch::build(QSGGeometry* g) const { return fill(g, boxAttributes(), v_); }

void CordBatch::add(QPointF a, QPointF b, const QColor& color, float width) {
    CordVertex v{};
    v.x0 = static_cast<float>(a.x());
    v.y0 = static_cast<float>(a.y());
    v.x1 = static_cast<float>(b.x());
    v.y1 = static_cast<float>(b.y());
    premultiplied(color, v.color);
    v.width = width;
    for (int i = 0; i < 4; ++i) {
        v.t = (i & 1) ? 1.0f : 0.0f;
        v.side = (i & 2) ? 1.0f : -1.0f;
        v_.push_back(v);
    }
}

QSGGeometry* CordBatch::build(QSGGeometry* g) const { return fill(g, cordAttributes(), v_); }

} // namespace qure::ui::sg
