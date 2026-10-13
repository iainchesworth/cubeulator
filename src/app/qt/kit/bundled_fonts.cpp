#include "bundled_fonts.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QSet>

namespace cube::app::kit {

namespace {
Q_LOGGING_CATEGORY(lcFonts, "cube.kit.fonts")

// Id of each resource file already registered, so a second call neither
// registers it twice nor loses track of its families.
QHash<QString, int>& registered() {
    static QHash<QString, int> files;
    return files;
}
}  // namespace

QStringList installBundledFonts() {
    QSet<QString> families;

    const QDir directory(QStringLiteral(":/fonts"));
    const QStringList files = directory.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
    if (files.isEmpty()) {
        qCWarning(lcFonts) << "No fonts are bundled in :/fonts; text falls back to whatever the system has";
    }

    for (const QString& file : files) {
        const QString path = directory.filePath(file);
        int id = -1;
        if (const auto known = registered().constFind(path); known != registered().cend()) {
            id = known.value();
        } else {
            id = QFontDatabase::addApplicationFont(path);
            registered().insert(path, id);
            if (id < 0) {
                qCWarning(lcFonts) << "Could not load bundled font" << path;
            }
        }
        if (id >= 0) {
            const QStringList names = QFontDatabase::applicationFontFamilies(id);
            families.unite(QSet<QString>(names.cbegin(), names.cend()));
        }
    }

    QStringList result(families.cbegin(), families.cend());
    result.sort();
    return result;
}

}  // namespace cube::app::kit
