#include "i18n/language_manager.hpp"

#include <QtQml/qqml.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

#include <utility>

namespace cube::i18n {

namespace {

constexpr auto kSettingsKey = "language/code";
// A code of the set, or "xx", read before the saved choice: the pseudo-locale
// QA fixture and the deterministic test runs use it.
constexpr auto kLocaleEnvOverride = "CUBEULATOR_LOCALE";
constexpr auto kLanguagesResource = ":/i18n/languages.json";

[[nodiscard]] LanguageInfo readLanguage(const QJsonObject& o, bool pseudo) {
    LanguageInfo info;
    info.code = o.value("code").toString();
    info.file = o.value("file").toString(info.code);
    info.tag = o.value("tag").toString(info.code);
    info.name = o.value("name").toString(info.code);
    info.plurals = o.value("plurals").toInt(2);
    info.rtl = o.value("rtl").toBool(false);
    info.translated = o.value("translated").toBool(false);
    info.pseudo = pseudo;
    return info;
}

[[nodiscard]] const LanguageInfo* find(const std::vector<LanguageInfo>& languages, const QString& code) {
    for (const LanguageInfo& l : languages) {
        if (!l.pseudo && l.code == code) {
            return &l;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<LanguageInfo> loadLanguages(const QByteArray& json) {
    QByteArray bytes = json;
    if (bytes.isEmpty()) {
        QFile file(QString::fromLatin1(kLanguagesResource));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("i18n: %s is not embedded in this build", kLanguagesResource);
            return {};
        }
        bytes = file.readAll();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return {};
    }
    std::vector<LanguageInfo> out;
    for (const QJsonValue& v : doc.object().value("languages").toArray()) {
        out.push_back(readLanguage(v.toObject(), false));
    }
    if (doc.object().contains("pseudo")) {
        out.push_back(readLanguage(doc.object().value("pseudo").toObject(), true));
    }
    return out;
}

QString canonicalCode(const QString& tag, const std::vector<LanguageInfo>& languages) {
    const QStringList parts = tag.trimmed().replace(QLatin1Char('_'), QLatin1Char('-')).split(QLatin1Char('-'));
    if (parts.isEmpty() || parts.first().isEmpty()) {
        return {};
    }
    QString language = parts.first().toLower();
    // The retired ISO 639 codes some systems still report.
    if (language == QLatin1String("iw")) {
        language = QStringLiteral("he");
    } else if (language == QLatin1String("ji")) {
        language = QStringLiteral("yi");
    }

    QString script;
    QString region;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString& p = parts.at(i);
        if (p.size() == 4) {
            script = p.toLower();
        } else if (p.size() == 2 || p.size() == 3) {
            region = p.toUpper();
        }
    }
    // Simplified Chinese is the one Chinese the set has. A device that asks
    // for Traditional Chinese (a script, or Taiwan, Hong Kong, Macao) gets the
    // next of its preferences rather than the wrong script.
    if (language == QLatin1String("zh")) {
        const bool traditional = script == QLatin1String("hant") || region == QLatin1String("TW") ||
                                 region == QLatin1String("HK") || region == QLatin1String("MO");
        return traditional ? QString{} : (find(languages, QStringLiteral("zh-CN")) != nullptr ? QStringLiteral("zh-CN")
                                                                                              : QString{});
    }
    for (const LanguageInfo& l : languages) {
        if (!l.pseudo && l.code.section(QLatin1Char('-'), 0, 0).toLower() == language) {
            return l.code;
        }
    }
    return {};
}

QString languageForUiLanguages(const QStringList& uiLanguages, const std::vector<LanguageInfo>& languages) {
    for (const QString& tag : uiLanguages) {
        if (const QString code = canonicalCode(tag, languages); !code.isEmpty()) {
            return code;
        }
    }
    return QStringLiteral("en");
}

LanguageManager::LanguageManager(QGuiApplication& app, QQmlEngine& engine, QString translationBasename,
                                 QObject* parent)
    : QObject(parent),
      app_(app),
      engine_(engine),
      basename_(std::move(translationBasename)),
      languages_(loadLanguages()) {
    // A build without the embedded set still has English, so that nothing
    // below has to ask whether the list is empty.
    if (find(languages_, QStringLiteral("en")) == nullptr) {
        LanguageInfo english;
        english.code = QStringLiteral("en");
        english.file = QStringLiteral("en");
        english.tag = QStringLiteral("en_GB");
        english.name = QStringLiteral("English");
        languages_.insert(languages_.begin(), english);
    }
    current_ = *find(languages_, QStringLiteral("en"));
}

void LanguageManager::applyInitialLanguage() {
    const QSettings settings;
    const QString saved = canonicalCode(settings.value(QLatin1String(kSettingsKey)).toString(), languages_);
    selected_ = saved;

    // The override first: it exists for the pseudo-locale and for runs that
    // must not depend on the machine's settings.
    const QString env = qEnvironmentVariable(kLocaleEnvOverride).trimmed();
    if (!env.isEmpty()) {
        for (const LanguageInfo& l : languages_) {
            if (l.code == env || l.file == env) {
                apply(l);
                return;
            }
        }
        if (const QString code = canonicalCode(env, languages_); !code.isEmpty()) {
            apply(*find(languages_, code));
            return;
        }
        qWarning("i18n: %s=%s is not a language of this app; ignored", kLocaleEnvOverride, qUtf8Printable(env));
    }

    if (!saved.isEmpty()) {
        apply(*find(languages_, saved));
        return;
    }
    const QString device = languageForUiLanguages(QLocale::system().uiLanguages(), languages_);
    apply(*find(languages_, device));
}

QVariantList LanguageManager::availableLanguages() const {
    QVariantList out;
    for (const LanguageInfo& l : languages_) {
        if (l.pseudo) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), l.code);
        entry.insert(QStringLiteral("name"), l.name);
        entry.insert(QStringLiteral("rtl"), l.rtl);
        out.append(entry);
    }
    return out;
}

QVariantMap LanguageManager::deviceLanguage() const {
    const QString code = languageForUiLanguages(QLocale::system().uiLanguages(), languages_);
    QVariantMap entry;
    entry.insert(QStringLiteral("code"), code);
    if (const LanguageInfo* l = find(languages_, code); l != nullptr) {
        entry.insert(QStringLiteral("name"), l->name);
    }
    return entry;
}

bool LanguageManager::hasOverride() const {
    const QSettings settings;
    return !canonicalCode(settings.value(QLatin1String(kSettingsKey)).toString(), languages_).isEmpty();
}

bool LanguageManager::setLanguage(const QString& code) {
    const QString canonical = canonicalCode(code, languages_);
    const LanguageInfo* language = canonical.isEmpty() ? nullptr : find(languages_, canonical);
    if (language == nullptr) {
        qWarning("LanguageManager::setLanguage: %s is not a language of this app", qUtf8Printable(code));
        return false;
    }
    {
        QSettings settings;
        settings.setValue(QLatin1String(kSettingsKey), language->code);
    }
    selected_ = language->code;
    apply(*language);
    return true;
}

void LanguageManager::useSystemLanguage() {
    {
        QSettings settings;
        settings.remove(QLatin1String(kSettingsKey));
    }
    selected_.clear();
    const QString device = languageForUiLanguages(QLocale::system().uiLanguages(), languages_);
    apply(*find(languages_, device));
}

void LanguageManager::apply(const LanguageInfo& language) {
    // QTranslator searches the one installed last first, so the order is the
    // fall-back order: Qt's own strings, the English catalogue (every id, and
    // so the floor under every other language), then the language itself.
    app_.removeTranslator(&qt_translator_);
    app_.removeTranslator(&base_translator_);
    app_.removeTranslator(&app_translator_);

    if (!language.pseudo) {
        if (qt_translator_.load(QLocale(language.tag), QStringLiteral("qt"), QStringLiteral("_"),
                                QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            app_.installTranslator(&qt_translator_);
        }
    }

    const QString prefix = QStringLiteral(":/i18n/") + basename_ + QLatin1Char('_');
    if (base_translator_.load(prefix + QStringLiteral("en.qm"))) {
        app_.installTranslator(&base_translator_);
    } else {
        qWarning("i18n: no English catalogue (%sen.qm): strings will show their ids", qUtf8Printable(prefix));
    }

    if (language.file != QLatin1String("en")) {
        // Loaded by exact name, never by QLocale matching: "xx" is not a
        // locale, and for a real one the name is the catalogue's own.
        if (app_translator_.load(prefix + language.file + QStringLiteral(".qm"))) {
            app_.installTranslator(&app_translator_);
        }
        // A catalogue with no finished entry is a valid, empty file; whether it
        // loads or not the English underneath answers.
    }

    app_.setLayoutDirection(language.rtl ? Qt::RightToLeft : Qt::LeftToRight);

    // Numbers, dates and times follow the app's language, not the operating
    // system's. Following the device, the device's own locale is kept when it
    // is the same language, so that a region's conventions (de_AT, ar_AE) are
    // not lost.
    QLocale locale(language.tag);
    if (selected_.isEmpty() && !language.pseudo) {
        const QLocale device = QLocale::system();
        if (canonicalCode(device.name(), languages_) == language.code) {
            locale = device;
        }
    }
    QLocale::setDefault(locale);

    current_ = language;
    engine_.setUiLanguage(locale.name());
    emit currentLanguageChanged();
    // After the signal, so that a binding which reads both the language and a
    // translated string sees the new language when the strings are re-read.
    engine_.retranslate();
}

LanguageManager* install(QGuiApplication& app, QQmlEngine& engine) {
    auto* manager = new LanguageManager(app, engine, QStringLiteral("cubeulator"), &engine);
    manager->applyInitialLanguage();
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "LanguageManager", manager);
    return manager;
}

}  // namespace cube::i18n
