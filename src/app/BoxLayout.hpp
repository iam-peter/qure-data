#pragma once

#include <QFont>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>

class QTextLayout;

namespace qure {
struct Box;
}

namespace qure::ui {

class FrontendRegistry;

/// Geometry rules for boxes, in canvas units (Pd pixels at zoom 1).
namespace layout {

constexpr double kPadX = 4;
constexpr double kPadY = 3;
constexpr double kIoletWidth = 8;
constexpr double kIoletHeight = 3;
constexpr double kIoletHotspot = 6; ///< vertical grab zone for connecting
constexpr int kMaxAutoChars = 60;   ///< Pd wraps automatic-width boxes here

const QFont& font();
double charWidth();
double lineHeight();

/// Splits box text into display lines: explicit line breaks, then greedy
/// word wrapping at `maxChars` (Pd's rule; the font is monospace, so this
/// is exact and much cheaper than shaping every box).
QStringList wrapText(const QString& text, int maxChars);

/// The text layout used for rendering one box's lines.
std::unique_ptr<QTextLayout> textLayout(const QStringList& lines);

/// Characters per line a box allows (fixed width or automatic).
int boxChars(const Box& box);

/// Recomputes box.w / box.h from its text or GUI class.
void layoutBox(Box& box, const FrontendRegistry& frontends);

QRectF boxRect(const Box& box);
QRectF inletRect(const Box& box, int index);
QRectF outletRect(const Box& box, int index);

} // namespace layout
} // namespace qure::ui
