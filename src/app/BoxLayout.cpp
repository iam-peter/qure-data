#include "BoxLayout.hpp"

#include "Fonts.hpp"
#include "FrontendRegistry.hpp"
#include "Patch.hpp"

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace qure::ui::layout {

const QFont& font() {
    static const QFont f = [] {
        loadFonts();
        QFont font(kMonoFamily);
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        if (!QFontInfo(font).fixedPitch())
            font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(12);
        return font;
    }();
    return f;
}

double charWidth() {
    static const double w = QFontMetricsF(font()).horizontalAdvance(QLatin1Char('m'));
    return w;
}

double lineHeight() {
    static const double h = std::ceil(QFontMetricsF(font()).height());
    return h;
}

QStringList wrapText(const QString& text, int maxChars) {
    QStringList lines;
    maxChars = std::max(1, maxChars);
    for (const auto& para : text.split(QLatin1Char('\n'))) {
        QString line;
        for (const auto& word : para.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            QString w = word;
            if (!line.isEmpty() && line.size() + 1 + w.size() > maxChars) {
                lines << line;
                line.clear();
            }
            while (w.size() > maxChars) { // hard-break very long words
                if (!line.isEmpty()) {
                    lines << line;
                    line.clear();
                }
                lines << w.left(maxChars);
                w = w.mid(maxChars);
            }
            line = line.isEmpty() ? w : line + QLatin1Char(' ') + w;
        }
        lines << line;
    }
    return lines;
}

std::unique_ptr<QTextLayout> textLayout(const QStringList& lines) {
    auto tl = std::make_unique<QTextLayout>(lines.join(QChar::LineSeparator), font());
    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    tl->setTextOption(opt);
    tl->beginLayout();
    double y = 0;
    for (;;) {
        QTextLine line = tl->createLine();
        if (!line.isValid())
            break;
        line.setPosition(QPointF(0, y));
        y += lineHeight();
    }
    tl->endLayout();
    return tl;
}

int boxChars(const Box& box) { return box.width > 0 ? box.width : kMaxAutoChars; }

namespace {

double arg(const Box& b, std::size_t i, double fallback) {
    // text[0] is the class name; arguments start at 1.
    if (i + 1 < b.text.size() && b.text[i + 1].isFloat())
        return b.text[i + 1].asFloat();
    return fallback;
}

double atomArg(const Box& b, std::size_t i, double fallback) {
    if (i < b.text.size() && b.text[i].isFloat())
        return b.text[i].asFloat();
    return fallback;
}

/// Sizes of Pd's own GUI classes, from their creation arguments.
std::optional<QSizeF> pdGuiSize(const Box& b) {
    const double cw = charWidth(), lh = lineHeight();
    switch (b.kind) {
    case BoxKind::FloatAtom:
    case BoxKind::SymbolAtom:
    case BoxKind::ListAtom: {
        const double def = b.kind == BoxKind::FloatAtom ? 5 : (b.kind == BoxKind::SymbolAtom ? 10 : 20);
        double chars = atomArg(b, 0, 0);
        if (chars <= 0)
            chars = def;
        return QSizeF(chars * cw + 2 * kPadX + 2, lh + 2 * kPadY);
    }
    case BoxKind::Object: break;
    default: return std::nullopt;
    }
    const auto cls = b.className().str();
    if (cls == "bng" || cls == "tgl" || cls == "toggle") {
        const double s = std::max(8.0, arg(b, 0, 19));
        return QSizeF(s, s);
    }
    if (cls == "hsl" || cls == "hslider")
        return QSizeF(std::max(8.0, arg(b, 0, 128)), std::max(8.0, arg(b, 1, 17)));
    if (cls == "vsl" || cls == "vslider")
        return QSizeF(std::max(8.0, arg(b, 0, 17)), std::max(8.0, arg(b, 1, 128)));
    if (cls == "knob") {
        const double s = std::max(16.0, arg(b, 0, 50));
        return QSizeF(s, s);
    }
    if (cls == "nbx" || cls == "numbox") {
        const double h = std::max(10.0, arg(b, 1, 17));
        return QSizeF(std::max(1.0, arg(b, 0, 5)) * cw + h / 2 + 6, h);
    }
    if (cls == "hradio" || cls == "hdl" || cls == "rdb" || cls == "radiobut" || cls == "radiobutton") {
        const double s = std::max(8.0, arg(b, 0, 18));
        return QSizeF(s * std::max(1.0, arg(b, 3, 8)), s);
    }
    if (cls == "vradio" || cls == "vdl") {
        const double s = std::max(8.0, arg(b, 0, 18));
        return QSizeF(s, s * std::max(1.0, arg(b, 3, 8)));
    }
    if (cls == "vu")
        return QSizeF(std::max(8.0, arg(b, 0, 18)), std::max(20.0, arg(b, 1, 160)));
    if (cls == "cnv" || cls == "my_canvas")
        return QSizeF(std::max(1.0, arg(b, 1, 100)), std::max(1.0, arg(b, 2, 60)));
    if (cls == "graph" && b.child)
        return QSizeF(b.child->props.pixW, b.child->props.pixH);
    return std::nullopt;
}

} // namespace

void layoutBox(Box& box, const FrontendRegistry& frontends) {
    if (box.kind == BoxKind::Raw || box.kind == BoxKind::Array) {
        box.w = box.h = 0;
        return;
    }
    if (!box.broken()) {
        if (auto s = pdGuiSize(box)) {
            box.w = static_cast<float>(s->width());
            box.h = static_cast<float>(s->height());
            return;
        }
        if (const auto* fe = frontends.find(box)) {
            double w = fe->size.width(), h = fe->size.height();
            if (fe->widthArg >= 0)
                w = arg(box, static_cast<std::size_t>(fe->widthArg), w / fe->widthScale) * fe->widthScale;
            if (fe->heightArg >= 0)
                h = arg(box, static_cast<std::size_t>(fe->heightArg), h / fe->heightScale) * fe->heightScale;
            box.w = static_cast<float>(std::max(8.0, w));
            box.h = static_cast<float>(std::max(8.0, h));
            return;
        }
    }

    // Text boxes: object, message, comment (and broken GUI objects).
    const QString text = QString::fromStdString(box.displayText());
    const auto lines = wrapText(text, boxChars(box));
    qsizetype longest = 0;
    for (const auto& l : lines)
        longest = std::max(longest, l.size());
    double chars = box.width > 0 ? box.width : static_cast<double>(longest);
    if (box.kind != BoxKind::Comment)
        chars = std::max(chars, 3.0);
    double w = chars * charWidth() + 1 + 2 * kPadX;
    if (box.kind == BoxKind::Message)
        w += 4; // room for the flag
    // Make sure iolets do not overlap.
    const int iolets = std::max(box.numInlets(), box.numOutlets());
    w = std::max(w, iolets * (kIoletWidth + 4.0) - 4.0);
    box.w = static_cast<float>(std::ceil(w));
    box.h = static_cast<float>(static_cast<double>(lines.size()) * lineHeight() + 2 * kPadY);
}

QRectF boxRect(const Box& box) { return {box.x, box.y, box.w, box.h}; }

namespace {
QRectF ioletRect(const Box& box, int index, int count, bool top) {
    const double span = box.w - kIoletWidth;
    const double x = count > 1 ? box.x + span * index / (count - 1) : box.x;
    const double y = top ? box.y : box.y + box.h - kIoletHeight;
    return {x, y, kIoletWidth, kIoletHeight};
}
} // namespace

QRectF inletRect(const Box& box, int index) { return ioletRect(box, index, box.numInlets(), true); }
QRectF outletRect(const Box& box, int index) { return ioletRect(box, index, box.numOutlets(), false); }

} // namespace qure::ui::layout
