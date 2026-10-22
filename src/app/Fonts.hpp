#pragma once

#include <QString>

namespace qure::ui {

/// The bundled typefaces: Inter for the interface, JetBrains Mono for boxes,
/// the console and anything else that is code. Both are SIL OFL fonts
/// (licenses in src/app/fonts).
inline const QString kSansFamily = QStringLiteral("Inter");
inline const QString kMonoFamily = QStringLiteral("JetBrains Mono");

/// Registers the bundled fonts with Qt; safe to call more than once.
/// Returns false when one of them could not be loaded.
bool loadFonts();

} // namespace qure::ui
