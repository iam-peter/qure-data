#include "Fonts.hpp"

#include <QFontDatabase>

namespace qure::ui {

bool loadFonts() {
    static const bool loaded = [] {
        bool ok = true;
        for (const char* file : {"Inter-Regular", "Inter-SemiBold", "Inter-Bold", "JetBrainsMono-Regular",
                                 "JetBrainsMono-Bold"})
            ok &= QFontDatabase::addApplicationFont(QStringLiteral(":/qure/fonts/%1.ttf").arg(QLatin1String(file))) >= 0;
        return ok;
    }();
    return loaded;
}

} // namespace qure::ui
