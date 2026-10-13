#include "catalogue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace cube::i18n_test {

namespace {

[[nodiscard]] QByteArray read_file(const QString& path) {
    QFile file(path);
    INFO("reading " << path.toStdString());
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

}  // namespace

const Entry* Catalogue::find(const QString& id) const {
    for (const Entry& e : entries) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

Catalogue read_catalogue(const QString& path) {
    Catalogue out;
    out.path = path;
    QXmlStreamReader xml(read_file(path));
    Entry current;
    bool in_message = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("TS")) {
                out.language = xml.attributes().value(QLatin1String("language")).toString();
            } else if (name == QLatin1String("message")) {
                in_message = true;
                current = Entry{};
                current.id = xml.attributes().value(QLatin1String("id")).toString();
                current.plural = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
            } else if (in_message && name == QLatin1String("source")) {
                current.source = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (in_message && name == QLatin1String("extracomment")) {
                current.note = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (in_message && name == QLatin1String("translation")) {
                current.type = xml.attributes().value(QLatin1String("type")).toString();
                if (current.plural) {
                    // The numerusforms are child elements; read until the
                    // translation closes.
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement() && xml.name() == QLatin1String("numerusform")) {
                            current.forms.append(xml.readElementText(QXmlStreamReader::IncludeChildElements));
                        } else if (xml.isEndElement() && xml.name() == QLatin1String("translation")) {
                            break;
                        }
                    }
                } else {
                    current.forms.append(xml.readElementText(QXmlStreamReader::IncludeChildElements));
                }
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
            out.entries.push_back(current);
            in_message = false;
        }
    }
    INFO("parsing " << path.toStdString() << ": " << xml.errorString().toStdString());
    REQUIRE_FALSE(xml.hasError());
    return out;
}

std::vector<Source> read_strings() {
    const QJsonDocument doc = QJsonDocument::fromJson(read_file(QStringLiteral(CUBE_STRINGS_JSON)));
    REQUIRE(doc.isObject());
    std::vector<Source> out;
    const QJsonObject object = doc.object();
    // QJsonObject sorts its keys; the file's own order is not needed for the
    // rules asserted here, which compare sets and look entries up by id.
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().startsWith(QLatin1Char('$'))) {
            continue;
        }
        const QJsonArray pair = it.value().toArray();
        REQUIRE(pair.size() == 2);
        Source s;
        s.id = it.key();
        s.note = pair.at(1).toString();
        if (pair.at(0).isObject()) {
            s.plural = true;
            const QJsonObject forms = pair.at(0).toObject();
            s.forms = {forms.value(QStringLiteral("one")).toString(), forms.value(QStringLiteral("other")).toString()};
        } else {
            s.forms = {pair.at(0).toString()};
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Language> read_languages() {
    const QJsonDocument doc = QJsonDocument::fromJson(read_file(QStringLiteral(CUBE_LANGUAGES_JSON)));
    REQUIRE(doc.isObject());
    std::vector<Language> out;
    auto parse = [](const QJsonObject& o, bool pseudo) {
        Language l;
        l.code = o.value(QStringLiteral("code")).toString();
        l.file = o.value(QStringLiteral("file")).toString();
        l.tag = o.value(QStringLiteral("tag")).toString();
        l.plurals = o.value(QStringLiteral("plurals")).toInt();
        l.rtl = o.value(QStringLiteral("rtl")).toBool();
        l.translated = o.value(QStringLiteral("translated")).toBool();
        l.pseudo = pseudo;
        return l;
    };
    for (const QJsonValue& v : doc.object().value(QStringLiteral("languages")).toArray()) {
        out.push_back(parse(v.toObject(), false));
    }
    out.push_back(parse(doc.object().value(QStringLiteral("pseudo")).toObject(), true));
    return out;
}

QString catalogue_path(const Language& language) {
    return QStringLiteral(CUBE_I18N_TS_DIR "/cubeulator_") + language.file + QStringLiteral(".ts");
}

QStringList argument_placeholders(const QString& text) {
    static const QRegularExpression re(QStringLiteral("%L?[1-9]"));
    QStringList found;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        // %L1 and %1 are the same argument.
        found.append(QStringLiteral("%") + it.next().captured(0).back());
    }
    found.sort();
    return found;
}

bool has_count_placeholder(const QString& text) {
    return text.contains(QLatin1String("%n")) || text.contains(QLatin1String("%Ln"));
}

QString locale_count(QString text) {
    return text.replace(QLatin1String("%n"), QLatin1String("%Ln"));
}

}  // namespace cube::i18n_test
