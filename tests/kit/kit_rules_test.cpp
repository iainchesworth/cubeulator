// The kit's own rules, checked on its source, so they hold for the component
// that is added next as well as for these:
//
//   * no user-visible words inside a component. Every one takes its text as a
//     property; translation (E2b) happens at the screen, and a string written
//     into the kit would be one nobody translates;
//   * no colour literal inside a component. A colour is a Theme token; the
//     gallery's bright "camera picture" backdrop is the one exception, and it
//     is developer-only;
//   * icons are Shape paths, never a Canvas;
//   * every component gives itself an objectName, so tests and a screen
//     reader's automation id can find it.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace {

QString kitDirectory() {
    return QStringLiteral(CUBE_KIT_SOURCE_DIR) + QStringLiteral("/src/app/qt/kit");
}

struct Source {
    QString file;
    QString text;
};

// Every kit QML file except the developer gallery, which is not shipped to
// anyone and is allowed English words and a white backdrop.
QList<Source> componentSources() {
    QList<Source> sources;
    const QDir directory(kitDirectory());
    const QStringList names = directory.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
    for (const QString& name : names) {
        if (name == QStringLiteral("KitGallery.qml")) {
            continue;
        }
        QFile file(directory.filePath(name));
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        sources.push_back({name, QString::fromUtf8(file.readAll())});
    }
    return sources;
}

// The text with comments taken out, so words in a comment are not a finding.
QString withoutComments(const QString& text) {
    QString result;
    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        const qsizetype slashes = line.indexOf(QStringLiteral("//"));
        if (slashes >= 0) {
            line.truncate(slashes);
        }
        result += line + '\n';
    }
    return result;
}

}  // namespace

TEST_CASE("No component writes user-visible words into itself", "[kit][rules]") {
    const QList<Source> sources = componentSources();
    REQUIRE(sources.size() > 20);

    // A property that carries words, given a literal with a letter in it.
    const QRegularExpression words(
        QStringLiteral(R"re(^\s*(text|title|heading|body|note|label|headline|explanation|backText|closeText|safeText|)re"
                       R"re(otherText|actionText|stateText|accessibleName|accessibleDescription)\s*:\s*"[^"]*[A-Za-z])re"),
        QRegularExpression::MultilineOption);

    for (const Source& source : sources) {
        const QString code = withoutComments(source.text);
        const QRegularExpressionMatch match = words.match(code);
        INFO(source.file.toStdString() << ": " << match.captured(0).trimmed().toStdString());
        CHECK_FALSE(match.hasMatch());
    }
}

TEST_CASE("No component writes a colour into itself", "[kit][rules]") {
    const QRegularExpression hex(QStringLiteral(R"re("#[0-9A-Fa-f]{3,8}")re"));
    for (const Source& source : componentSources()) {
        const QString code = withoutComments(source.text);
        const QRegularExpressionMatch match = hex.match(code);
        INFO(source.file.toStdString() << ": " << match.captured(0).toStdString());
        CHECK_FALSE(match.hasMatch());
    }
}

TEST_CASE("Icons are Shape paths, not a Canvas", "[kit][rules]") {
    for (const Source& source : componentSources()) {
        INFO(source.file.toStdString());
        CHECK_FALSE(withoutComments(source.text).contains(QRegularExpression(QStringLiteral(R"re(\bCanvas\b)re"))));
    }
}

TEST_CASE("Every component names itself", "[kit][rules]") {
    // Parts and singletons that are not things a test or a reader finds by
    // name.
    const QStringList exempt{"KitIcons.qml", "Navigator.qml", "KitWindowBindings.qml", "KitFocusRing.qml"};
    for (const Source& source : componentSources()) {
        if (exempt.contains(source.file)) {
            continue;
        }
        INFO(source.file.toStdString());
        CHECK(source.text.contains(QStringLiteral("objectName:")));
    }
}

TEST_CASE("Every component sets its own accessible role or hides itself", "[kit][rules][a11y]") {
    // Components that draw something must either say what they are to a screen
    // reader (Accessible.role) or say they are decoration (Accessible.ignored).
    const QStringList exempt{"KitIcons.qml", "Navigator.qml", "KitWindowBindings.qml"};
    for (const Source& source : componentSources()) {
        if (exempt.contains(source.file)) {
            continue;
        }
        INFO(source.file.toStdString());
        // One that is a Kit* type itself (the swatch is a sticker) has its
        // base's role.
        static const QRegularExpression derived(QStringLiteral(R"re(^Kit\w+\s*\{)re"),
                                                QRegularExpression::MultilineOption);
        const bool inherits = derived.match(source.text).hasMatch();
        const bool says = inherits || source.text.contains(QStringLiteral("Accessible.role")) ||
                          source.text.contains(QStringLiteral("Accessible.ignored"));
        CHECK(says);
    }
}
