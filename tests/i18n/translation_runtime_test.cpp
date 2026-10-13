// What a person would see: the compiled catalogues, installed the way the app
// installs them, asked for every id in every language.
//
// The rule these hold is the one the plan asks of E2b: a missing translation
// shows the English, and a raw id never reaches the screen.
#include "catalogue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QTemporaryDir>
#include <QTranslator>

using namespace cube::i18n_test;

namespace {

[[nodiscard]] std::string str(const QString& s) {
    return s.toStdString();
}

// An id the way a call site writes it.
[[nodiscard]] QString tr_id(const QString& id, int n = -1) {
    return qtTrId(id.toUtf8().constData(), n);
}

// The English catalogue under one language's, as the language manager
// installs them (the last installed is searched first).
struct Installed {
    QTranslator base;
    QTranslator language;

    Installed(const QString& file, bool with_base = true) {
        if (with_base) {
            REQUIRE(base.load(QStringLiteral(":/i18n/cubeulator_en.qm")));
            QCoreApplication::installTranslator(&base);
        }
        if (file != QLatin1String("en")) {
            // A catalogue with nothing finished in it is a valid, empty file.
            if (language.load(QStringLiteral(":/i18n/cubeulator_") + file + QStringLiteral(".qm"))) {
                QCoreApplication::installTranslator(&language);
            }
        }
    }
    ~Installed() {
        QCoreApplication::removeTranslator(&language);
        QCoreApplication::removeTranslator(&base);
    }
    Installed(const Installed&) = delete;
    Installed& operator=(const Installed&) = delete;
};

const int kCounts[] = {0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 21, 22, 100, 101, 102, 111, 1000};

}  // namespace

TEST_CASE("with no catalogue installed an id comes back as itself", "[i18n][runtime]") {
    // The control for the cases below. If this did not hold, "the result is
    // not the id" would prove nothing.
    CHECK(tr_id(QStringLiteral("home.scan")) == QLatin1String("home.scan"));
}

TEST_CASE("a raw id never reaches the screen, in any language", "[i18n][runtime]") {
    const auto strings = read_strings();
    for (const Language& l : read_languages()) {
        Installed installed(l.file);
        for (const Source& s : strings) {
            INFO(str(l.code) << " / " << str(s.id));
            const QString shown = tr_id(s.id);
            CHECK_FALSE(shown.isEmpty());
            CHECK(shown != s.id);
        }
    }
}

TEST_CASE("a language with no translation yet shows the English", "[i18n][runtime][fallback]") {
    const auto strings = read_strings();
    for (const Language& l : read_languages()) {
        if (l.translated || l.pseudo) {
            continue;
        }
        Installed installed(l.file);
        for (const Source& s : strings) {
            if (s.plural) {
                continue;
            }
            INFO(str(l.code) << " / " << str(s.id));
            CHECK(tr_id(s.id) == locale_count(s.forms.front()));
        }
    }
}

TEST_CASE("English shows strings.json, and the plural forms are chosen by the count", "[i18n][runtime][plural]") {
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QStringLiteral("en_GB")));
    Installed installed(QStringLiteral("en"));
    for (const Source& s : read_strings()) {
        INFO(str(s.id));
        if (!s.plural) {
            CHECK(tr_id(s.id) == locale_count(s.forms.front()));
            continue;
        }
        QString one = locale_count(s.forms.at(0));
        QString other = locale_count(s.forms.at(1));
        one.replace(QLatin1String("%Ln"), QStringLiteral("1"));
        other.replace(QLatin1String("%Ln"), QStringLiteral("5"));
        CHECK(tr_id(s.id, 1) == one);
        CHECK(tr_id(s.id, 5) == other);
    }
    QLocale::setDefault(previous);
}

TEST_CASE("every plural string resolves for every count in every language", "[i18n][runtime][plural]") {
    const QLocale previous = QLocale();
    for (const Language& l : read_languages()) {
        QLocale::setDefault(QLocale(l.tag));
        Installed installed(l.file);
        for (const Source& s : read_strings()) {
            if (!s.plural) {
                continue;
            }
            for (const int n : kCounts) {
                INFO(str(l.code) << " / " << str(s.id) << " / n=" << n);
                const QString shown = tr_id(s.id, n);
                CHECK_FALSE(shown.isEmpty());
                CHECK(shown != s.id);
                // The count was substituted, in whatever digits the language
                // writes: nothing of the placeholder is left.
                CHECK_FALSE(shown.contains(QLatin1String("%n")));
                CHECK_FALSE(shown.contains(QLatin1String("%Ln")));
            }
        }
    }
    QLocale::setDefault(previous);
}

TEST_CASE("the pseudo-locale answers every id with the bracketed, accented string", "[i18n][runtime][pseudo]") {
    Installed installed(QStringLiteral("xx"));
    const QString shown = tr_id(QStringLiteral("home.scan"));
    CHECK(shown.startsWith(QLatin1Char('[')));
    CHECK(shown.endsWith(QLatin1Char(']')));
    // And the English under it still answers an id the pseudo-locale lacks.
    CHECK_FALSE(tr_id(QStringLiteral("app.name")).isEmpty());
}

// Qt's own lrelease puts each language's plural rule into the catalogue it
// writes, and QTranslator applies it. Rather than trust that this matches the
// language, write a catalogue per language whose forms are named after their
// index and ask which one each count gets. The expectations are the CLDR's
// categories for the form counts Qt's table gives (languages.json: "plurals").
TEST_CASE("each language's plural rule picks the form its language has", "[i18n][runtime][plural]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QLocale previous = QLocale();

    struct Expect {
        QString code;
        // index of the form for a count
        int (*form)(int);
    };
    auto english_like = [](int n) { return n == 1 ? 0 : 1; };
    auto french = [](int n) { return n <= 1 ? 0 : 1; };
    auto single = [](int) { return 0; };
    auto arabic = [](int n) {
        if (n == 0) return 0;
        if (n == 1) return 1;
        if (n == 2) return 2;
        const int r = n % 100;
        if (r >= 3 && r <= 10) return 3;
        if (r >= 11 && r <= 99) return 4;
        return 5;
    };
    const Expect expectations[] = {
        {QStringLiteral("en"), +english_like}, {QStringLiteral("de"), +english_like},
        {QStringLiteral("es"), +english_like}, {QStringLiteral("fr"), +french},
        {QStringLiteral("ja"), +single},       {QStringLiteral("zh-CN"), +single},
        // Qt's table has two forms for Hebrew and Yiddish: one, and the rest.
        {QStringLiteral("he"), +english_like}, {QStringLiteral("yi"), +english_like},
        {QStringLiteral("ar"), +arabic},
    };

    for (const Language& l : read_languages()) {
        if (l.pseudo) {
            continue;
        }
        const Expect* expect = nullptr;
        for (const Expect& e : expectations) {
            if (e.code == l.code) {
                expect = &e;
            }
        }
        INFO(str(l.code));
        REQUIRE(expect != nullptr);

        QString forms;
        for (int i = 0; i < l.plurals; ++i) {
            forms += QStringLiteral("<numerusform>form%1</numerusform>").arg(i);
        }
        const QString ts = dir.filePath(QStringLiteral("plural_%1.ts").arg(l.file));
        const QString qm = dir.filePath(QStringLiteral("plural_%1.qm").arg(l.file));
        QFile out(ts);
        REQUIRE(out.open(QIODevice::WriteOnly));
        out.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><!DOCTYPE TS>"
                                 "<TS version=\"2.1\" language=\"%1\" sourcelanguage=\"en_GB\"><context><name></name>"
                                 "<message id=\"t.plural\" numerus=\"yes\"><source>%Ln thing</source>"
                                 "<translation>%2</translation></message></context></TS>")
                      .arg(l.tag, forms)
                      .toUtf8());
        out.close();

        QProcess lrelease;
        lrelease.start(QStringLiteral(CUBE_LRELEASE_EXECUTABLE),
                       {QStringLiteral("-idbased"), QStringLiteral("-silent"), ts, QStringLiteral("-qm"), qm});
        REQUIRE(lrelease.waitForFinished(30000));
        REQUIRE(lrelease.exitCode() == 0);

        QTranslator translator;
        REQUIRE(translator.load(qm));
        QCoreApplication::installTranslator(&translator);
        // Digits do not matter here; the form does.
        QLocale::setDefault(QLocale(QStringLiteral("en_GB")));
        for (int n = 0; n <= 130; ++n) {
            INFO(str(l.code) << " n=" << n);
            CHECK(tr_id(QStringLiteral("t.plural"), n) == QStringLiteral("form%1").arg(expect->form(n)));
        }
        QCoreApplication::removeTranslator(&translator);
    }
    QLocale::setDefault(previous);
}
