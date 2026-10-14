#include "bundled_fonts.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QHash>
#include <QLoggingCategory>
#include <QSet>

namespace cube::app::kit {

namespace {
Q_LOGGING_CATEGORY(lcFonts, "cube.kit.fonts")

// Id Qt gave each resource file, so a second call neither registers it twice
// nor loses track of its families. An id is only trusted while Qt still
// reports families for it: removeAllApplicationFonts() (a test does this to
// stand in for a machine with nothing installed) invalidates every id.
QHash<QString, int>& registered() {
    static QHash<QString, int> files;
    return files;
}
}  // namespace

QStringList installBundledFonts() {
    QSet<QString> families;

    const QDir directory(QStringLiteral(":/fonts"));
    const QStringList files =
        directory.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
    if (files.isEmpty()) {
        qCDebug(lcFonts) << "No fonts are bundled in :/fonts; text uses the system's";
    }

    for (const QString& file : files) {
        const QString path = directory.filePath(file);
        int id = -1;
        if (const auto known = registered().constFind(path); known != registered().cend()) {
            id = known.value();
            if (id >= 0 && QFontDatabase::applicationFontFamilies(id).isEmpty()) {
                id = -1;  // taken away since; register it again
            }
        }
        if (id < 0) {
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
