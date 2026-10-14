// The language manager: what it does on start-up, what one call to
// setLanguage() changes, and that it changes all of it before anything is
// re-read. The family's tst_LanguageSwitching.qml is the model; this is the
// part of it that needs no screen (the screens are in screen_test.cpp).
#include "i18n/language_manager.hpp"
#include "catalogue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>

using cube::i18n::canonicalCode;
using cube::i18n::languageForUiLanguages;
using cube::i18n::LanguageManager;
using cube::i18n::loadLanguages;

namespace {

[[nodiscard]] std::string str(const QString& s) {
    return s.toStdString();
}

void forget_saved_language() {
    QSettings settings;
    settings.remove(QStringLiteral("language/code"));
    settings.sync();
}

[[nodiscard]] QString saved_language() {
    const QSettings settings;
    return settings.value(QStringLiteral("language/code")).toString();
}

// A clean start: no saved choice, no override.
struct Fresh {
    Fresh() {
        forget_saved_language();
        qunsetenv("CUBEULATOR_LOCALE");
    }
    ~Fresh() {
        forget_saved_language();
        qunsetenv("CUBEULATOR_LOCALE");
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
        QLocale::setDefault(QLocale::system());
    }
};

}  // namespace

TEST_CASE("the languages of the app are the nine, English first, in their own names",
          "[i18n][manager]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    const QVariantList list = manager.availableLanguages();

    QStringList codes;
    QStringList names;
    for (const QVariant& v : list) {
        codes << v.toMap().value(QStringLiteral("code")).toString();
        names << v.toMap().value(QStringLiteral("name")).toString();
    }
    CHECK(codes == QStringList{QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("fr"),
                               QStringLiteral("es"), QStringLiteral("ja"), QStringLiteral("zh-CN"),
                               QStringLiteral("he"), QStringLiteral("ar"), QStringLiteral("yi")});
    CHECK(names == QStringList{QStringLiteral("English"), QStringLiteral("Deutsch"),
                               QStringLiteral("Français"), QStringLiteral("Español"),
                               QStringLiteral("日本語"), QStringLiteral("简体中文"),
                               QStringLiteral("עברית"), QStringLiteral("العربية"),
                               QStringLiteral("יידיש")});
    // The pseudo-locale is a fixture, never a choice.
    CHECK_FALSE(codes.contains(QStringLiteral("xx")));
}

TEST_CASE("the manager's set is the set the catalogues are made for", "[i18n][manager]") {
    const auto mine = loadLanguages();
    const auto theirs = cube::i18n_test::read_languages();
    REQUIRE(mine.size() == theirs.size());
    for (std::size_t i = 0; i < mine.size(); ++i) {
        INFO(str(theirs[i].code));
        CHECK(mine[i].code == theirs[i].code);
        CHECK(mine[i].file == theirs[i].file);
        CHECK(mine[i].plurals == theirs[i].plurals);
        CHECK(mine[i].rtl == theirs[i].rtl);
        // languages.json's idea of direction is checked against Qt's.
        CHECK(QLocale(mine[i].tag).textDirection() ==
              (mine[i].rtl ? Qt::RightToLeft : Qt::LeftToRight));
    }
}

TEST_CASE("a language is recognised however a system writes it", "[i18n][manager]") {
    const auto languages = loadLanguages();
    struct Row {
        const char* tag;
        const char* code;
    };
    const Row rows[] = {
        {"en", "en"},
        {"EN-gb", "en"},
        {"de-AT", "de"},
        {"fr_CA", "fr"},
        {"es-419", "es"},
        {"ja_JP", "ja"},
        {"zh", "zh-CN"},
        {"zh_CN", "zh-CN"},
        {"zh-Hans-CN", "zh-CN"},
        {"zh-SG", "zh-CN"},
        // Traditional Chinese is not one of the nine; it is not guessed at.
        {"zh-TW", ""},
        {"zh-Hant", ""},
        {"zh-HK", ""},
        {"he_IL", "he"},
        {"iw", "he"},
        {"ar-EG", "ar"},
        {"yi", "yi"},
        {"ji", "yi"},
        {"pt-BR", ""},
        {"xx", ""},
        {"", ""},
    };
    for (const Row& r : rows) {
        INFO(r.tag);
        CHECK(canonicalCode(QString::fromLatin1(r.tag), languages) == QString::fromLatin1(r.code));
    }
}

TEST_CASE("the device's language is the first of its preferences the app has", "[i18n][manager]") {
    const auto languages = loadLanguages();
    CHECK(languageForUiLanguages(
              {QStringLiteral("pt-BR"), QStringLiteral("nl-NL"), QStringLiteral("de-DE")},
              languages) == QLatin1String("de"));
    CHECK(languageForUiLanguages({QStringLiteral("zh-Hant-TW"), QStringLiteral("ja-JP")},
                                 languages) == QLatin1String("ja"));
    CHECK(languageForUiLanguages({QStringLiteral("zh-Hant-TW")}, languages) == QLatin1String("en"));
    CHECK(languageForUiLanguages({QStringLiteral("sv"), QStringLiteral("da")}, languages) ==
          QLatin1String("en"));
    CHECK(languageForUiLanguages({}, languages) == QLatin1String("en"));
}

TEST_CASE("start-up: the environment, then the saved choice, then the device", "[i18n][manager]") {
    Fresh fresh;
    QQmlEngine engine;

    SECTION("the device, when nothing else says") {
        LanguageManager manager(*qGuiApp, engine);
        manager.applyInitialLanguage();
        // Whatever this machine's language is, it is one of the nine.
        QStringList codes;
        for (const QVariant& v : manager.availableLanguages()) {
            codes << v.toMap().value(QStringLiteral("code")).toString();
        }
        CHECK(codes.contains(manager.currentLanguage()));
        CHECK(manager.selectedLanguage().isEmpty());
        CHECK_FALSE(manager.hasOverride());
    }
    SECTION("a saved choice beats the device") {
        QSettings().setValue(QStringLiteral("language/code"), QStringLiteral("ar"));
        LanguageManager manager(*qGuiApp, engine);
        manager.applyInitialLanguage();
        CHECK(manager.currentLanguage() == QLatin1String("ar"));
        CHECK(manager.rtl());
        CHECK(manager.hasOverride());
        CHECK(qGuiApp->layoutDirection() == Qt::RightToLeft);
    }
    SECTION("the environment beats a saved choice, and the saved choice is left alone") {
        QSettings().setValue(QStringLiteral("language/code"), QStringLiteral("ar"));
        qputenv("CUBEULATOR_LOCALE", "de");
        LanguageManager manager(*qGuiApp, engine);
        manager.applyInitialLanguage();
        CHECK(manager.currentLanguage() == QLatin1String("de"));
        CHECK(manager.selectedLanguage() == QLatin1String("ar"));
        CHECK(saved_language() == QLatin1String("ar"));
    }
    SECTION("the environment can ask for the pseudo-locale, which no choice can") {
        qputenv("CUBEULATOR_LOCALE", "xx");
        LanguageManager manager(*qGuiApp, engine);
        manager.applyInitialLanguage();
        CHECK(manager.currentLanguage() == QLatin1String("xx"));
        CHECK_FALSE(manager.rtl());
        CHECK(qtTrId("home.scan").startsWith(QLatin1Char('[')));
        CHECK_FALSE(manager.setLanguage(QStringLiteral("xx")));
    }
    SECTION("a code the app does not have is ignored, in the environment and in the store") {
        QSettings().setValue(QStringLiteral("language/code"), QStringLiteral("tlh"));
        qputenv("CUBEULATOR_LOCALE", "tlh");
        LanguageManager manager(*qGuiApp, engine);
        manager.applyInitialLanguage();
        CHECK_FALSE(manager.hasOverride());
        CHECK(manager.selectedLanguage().isEmpty());
    }
}

TEST_CASE("setLanguage changes everything a switch touches, and saves it", "[i18n][manager]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    manager.applyInitialLanguage();
    REQUIRE(manager.setLanguage(QStringLiteral("en")));
    REQUIRE_FALSE(manager.rtl());

    int announced = 0;
    QObject::connect(&manager, &LanguageManager::currentLanguageChanged, &manager,
                     [&announced]() { ++announced; });

    SECTION("to a right-to-left language and back") {
        REQUIRE(manager.setLanguage(QStringLiteral("he")));
        CHECK(manager.currentLanguage() == QLatin1String("he"));
        CHECK(manager.rtl());
        CHECK(qGuiApp->layoutDirection() == Qt::RightToLeft);
        CHECK(QLocale().language() == QLocale::Hebrew);
        CHECK(saved_language() == QLatin1String("he"));
        CHECK(manager.hasOverride());
        CHECK(manager.selectedLanguage() == QLatin1String("he"));
        CHECK(announced >= 1);

        REQUIRE(manager.setLanguage(QStringLiteral("en")));
        CHECK_FALSE(manager.rtl());
        CHECK(qGuiApp->layoutDirection() == Qt::LeftToRight);
        CHECK(saved_language() == QLatin1String("en"));
    }
    SECTION("all three right-to-left languages mirror, and nothing else does") {
        for (const char* code : {"he", "ar", "yi"}) {
            INFO(code);
            REQUIRE(manager.setLanguage(QString::fromLatin1(code)));
            CHECK(manager.rtl());
            CHECK(qGuiApp->layoutDirection() == Qt::RightToLeft);
        }
        for (const char* code : {"en", "de", "fr", "es", "ja", "zh-CN"}) {
            INFO(code);
            REQUIRE(manager.setLanguage(QString::fromLatin1(code)));
            CHECK_FALSE(manager.rtl());
            CHECK(qGuiApp->layoutDirection() == Qt::LeftToRight);
        }
    }
    SECTION("a code that is not one of the nine changes nothing") {
        REQUIRE(manager.setLanguage(QStringLiteral("de")));
        CHECK_FALSE(manager.setLanguage(QStringLiteral("tlh")));
        CHECK_FALSE(manager.setLanguage(QStringLiteral("xx")));
        CHECK_FALSE(manager.setLanguage(QString()));
        CHECK(manager.currentLanguage() == QLatin1String("de"));
        CHECK(saved_language() == QLatin1String("de"));
    }
    SECTION("the device's language again") {
        REQUIRE(manager.setLanguage(QStringLiteral("ar")));
        manager.useSystemLanguage();
        CHECK_FALSE(manager.hasOverride());
        CHECK(manager.selectedLanguage().isEmpty());
        CHECK(saved_language().isEmpty());
    }
    SECTION("the direction and the locale are already new when the change is announced") {
        // A binding re-evaluated on the signal reads direction, locale and
        // language together; none may still be the language being left.
        bool direction_new = false;
        bool locale_new = false;
        bool language_new = false;
        QObject::connect(&manager, &LanguageManager::currentLanguageChanged, &manager, [&]() {
            direction_new = qGuiApp->layoutDirection() == Qt::RightToLeft;
            locale_new = QLocale().language() == QLocale::Arabic;
            language_new = manager.currentLanguage() == QLatin1String("ar");
        });
        REQUIRE(manager.setLanguage(QStringLiteral("ar")));
        CHECK(direction_new);
        CHECK(locale_new);
        CHECK(language_new);
    }
}

TEST_CASE("numbers, dates and times follow the app's language", "[i18n][manager][locale]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    manager.applyInitialLanguage();
    const QDate date(2026, 10, 13);

    // Each assertion holds whatever the operating system is set to: it is
    // the app's language that decides.
    REQUIRE(manager.setLanguage(QStringLiteral("en")));
    CHECK(QLocale().toString(1234.5, 'f', 1) == QLatin1String("1,234.5"));
    CHECK(QLocale().toString(date, QLocale::LongFormat).contains(QLatin1String("October")));
    CHECK(engine.uiLanguage() == QLocale().name());

    REQUIRE(manager.setLanguage(QStringLiteral("de")));
    CHECK(QLocale().toString(1234.5, 'f', 1) == QLatin1String("1.234,5"));
    CHECK(QLocale().toString(date, QLocale::LongFormat).contains(QLatin1String("Oktober")));
    CHECK(engine.uiLanguage() == QLocale().name());

    REQUIRE(manager.setLanguage(QStringLiteral("fr")));
    CHECK(QLocale().toString(date, QLocale::LongFormat).contains(QLatin1String("octobre")));

    // A plural string's count is written in the app's language too: the
    // digits of Arabic are not the digits of English.
    REQUIRE(manager.setLanguage(QStringLiteral("ar")));
    CHECK(QLocale().language() == QLocale::Arabic);
    CHECK(QLocale().toString(3) == QLocale(QStringLiteral("ar")).toString(3));
    CHECK(qtTrId("scan.leave.body", 3).contains(QLocale().toString(3)));
}

TEST_CASE("a string with no translation shows its English through the manager",
          "[i18n][manager][fallback]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    manager.applyInitialLanguage();

    // None of these three is translated yet (E5): each must show the English.
    for (const char* code : {"he", "ar", "yi"}) {
        INFO(code);
        REQUIRE(manager.setLanguage(QString::fromLatin1(code)));
        CHECK(qtTrId("home.scan") == QLatin1String("Scan a cube"));
        CHECK(qtTrId("common.back") == QLatin1String("Back"));
    }
}

TEST_CASE("a Latin-script draft shows its translation through the manager",
          "[i18n][manager][translated]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    manager.applyInitialLanguage();

    // German, French and Spanish are complete drafts: the person sees them
    // and not the English under them. No word is asserted (a reviewer will
    // change some); that it is not the English, and that the product name and
    // the count survive, is what the manager owes.
    for (const char* code : {"de", "fr", "es"}) {
        INFO(code);
        REQUIRE(manager.setLanguage(QString::fromLatin1(code)));
        for (const char* id : {"home.scan", "common.back", "settings.section.camera"}) {
            INFO(id);
            const QString shown = qtTrId(id);
            CHECK_FALSE(shown.isEmpty());
            CHECK(shown != QLatin1String(id));
        }
        CHECK(qtTrId("home.scan") != QLatin1String("Scan a cube"));
        CHECK(qtTrId("common.back") != QLatin1String("Back"));
        CHECK(qtTrId("app.name") == QLatin1String("Cubeulator"));
        // The count is written in the language's own number format, and the
        // singular is not the plural.
        const QString three = qtTrId("scan.leave.body", 3);
        CHECK(three.contains(QLocale().toString(3)));
        CHECK(qtTrId("scan.leave.body", 1) != three);
    }
}

TEST_CASE("a draft language shows its translation through the manager",
          "[i18n][manager][translated]") {
    Fresh fresh;
    QQmlEngine engine;
    LanguageManager manager(*qGuiApp, engine);
    manager.applyInitialLanguage();

    // Japanese and Simplified Chinese are complete drafts: the person sees
    // them, in Han and kana, and not the English under them.
    auto is_cjk = [](const QString& s) { return !s.isEmpty() && s.front().unicode() >= 0x3000; };
    for (const char* code : {"ja", "zh-CN"}) {
        INFO(code);
        REQUIRE(manager.setLanguage(QString::fromLatin1(code)));
        for (const char* id : {"home.scan", "common.back", "settings.section.language"}) {
            INFO(id);
            const QString shown = qtTrId(id);
            CHECK(is_cjk(shown));
        }
        // The product name is not translated.
        CHECK(qtTrId("app.name") == QLatin1String("Cubeulator"));
        // A count is written in the language's own number format.
        CHECK(qtTrId("scan.leave.body", 3).contains(QLocale().toString(3)));
    }
}
