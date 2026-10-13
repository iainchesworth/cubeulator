#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace cube::i18n {

// One language of the set (src/app/i18n/languages.json).
struct LanguageInfo {
    QString code;  // what is stored under language/code, e.g. "zh-CN"
    QString file;  // the catalogue: cubeulator_<file>.qm
    QString tag;   // the QLocale name numbers and dates follow, e.g. "zh_CN"
    QString name;  // the language written in itself; never translated
    int plurals = 2;
    bool rtl = false;
    bool translated = false;
    bool pseudo = false;
};

// The set, from `json` or - when it is empty - from the :/i18n/languages.json
// the build embeds. English is first and the pseudo-locale is last, with
// `pseudo` set. Empty on a document that does not parse.
[[nodiscard]] std::vector<LanguageInfo> loadLanguages(const QByteArray& json = {});

// `tag` ("zh_CN", "zh-Hans-CN", "de-AT", "iw") as one of the set's codes, or an
// empty string when the set has no language for it. The pseudo-locale is not
// matched here: it is reached only through the environment override.
[[nodiscard]] QString canonicalCode(const QString& tag, const std::vector<LanguageInfo>& languages);

// The first of `uiLanguages` (a device's preference order, as
// QLocale::uiLanguages() gives it) the set has a language for; "en" if none.
[[nodiscard]] QString languageForUiLanguages(const QStringList& uiLanguages,
                                             const std::vector<LanguageInfo>& languages);

// Owns everything a language switch touches, so that a switch is one call and
// needs no restart: the app's translators (English underneath, the chosen
// language on top, then Qt's own), the saved choice, the application's layout
// direction, the default locale numbers and dates follow, and the running
// engine's translations.
//
// What it does not do is choose the font. It announces the language and the
// direction (currentLanguage, rtl); Theme.qml, which owns the faces, picks the
// family for the script (the kit session's seam - docs/i18n.md).
//
// Ported from the family's LanguageManager (ICL Forge,
// apps/shared/preferences/src/language_manager.{hpp,cpp}, itself modelled on
// Countdown Solver's) - same start-up order, same shape, same rule that the
// language list is endonyms and never translated. Adapted:
//   * ids, not source text: every string the app shows is qsTrId(), and the
//     English catalogue is installed under every language, so a string with no
//     translation shows its English and never its id;
//   * numbers, dates and times follow the app's language (QLocale::setDefault,
//     which the family's never calls), not the operating system's;
//   * the device language is the first of the device's *preferences* the set
//     has, not only the device's first language;
//   * the font is Theme.qml's, not the application font's.
//
// Registered with QML as the "Cubeulator" singleton `LanguageManager` by
// install() below.
class LanguageManager : public QObject {
    Q_OBJECT
    // The language in force: a code from the set, or "xx" for the
    // pseudo-locale.
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY currentLanguageChanged)
    // What the person chose, as stored: "" is "same as this device".
    Q_PROPERTY(QString selectedLanguage READ selectedLanguage NOTIFY currentLanguageChanged)
    // The language in force is written right to left (Hebrew, Arabic, Yiddish).
    Q_PROPERTY(bool rtl READ rtl NOTIFY currentLanguageChanged)

public:
    // `app` and `engine` must outlive this object. `translationBasename` names
    // the catalogues under :/i18n/ ("cubeulator" gives cubeulator_de.qm).
    explicit LanguageManager(QGuiApplication& app, QQmlEngine& engine,
                             QString translationBasename = QStringLiteral("cubeulator"),
                             QObject* parent = nullptr);

    // The initial language: CUBEULATOR_LOCALE (any code of the set, or "xx"),
    // then the saved language/code, then the device, then English. Called once,
    // before the QML loads, so that the first frame is already translated and
    // mirrored.
    void applyInitialLanguage();

    [[nodiscard]] QString currentLanguage() const { return current_.code; }
    [[nodiscard]] QString selectedLanguage() const { return selected_; }
    [[nodiscard]] bool rtl() const { return current_.rtl; }

    // One { code, name } per language of the set, English first, each name in
    // its own language and never translated. The pseudo-locale is never in it.
    Q_INVOKABLE QVariantList availableLanguages() const;
    // { code, name } of the language the device would give: what the "Same as
    // this device (%1)" row names.
    Q_INVOKABLE QVariantMap deviceLanguage() const;
    // Saves `code`, and switches. False, and nothing changed, if `code` is not
    // one of availableLanguages(). The pseudo-locale is refused.
    Q_INVOKABLE bool setLanguage(const QString& code);
    // Forgets the saved choice and follows the device again.
    Q_INVOKABLE void useSystemLanguage();
    // Whether a choice is saved (false: following the device).
    Q_INVOKABLE bool hasOverride() const;

signals:
    void currentLanguageChanged();

private:
    void apply(const LanguageInfo& language);

    QGuiApplication& app_;
    QQmlEngine& engine_;
    QString basename_;
    std::vector<LanguageInfo> languages_;
    QTranslator qt_translator_;
    QTranslator base_translator_;
    QTranslator app_translator_;
    LanguageInfo current_;
    QString selected_;
};

// What the shell does to be translated, in one place: makes the manager (owned
// by `engine`), applies the initial language and registers it with QML. Call it
// after the QGuiApplication and before the engine loads Main.qml. Returns the
// manager.
LanguageManager* install(QGuiApplication& app, QQmlEngine& engine);

}  // namespace cube::i18n
