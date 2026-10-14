#pragma once

#include <QString>
#include <QStringList>

#include <vector>

// Reads the translation catalogues and strings.json as files, with no
// translator involved, so that the rules about what a catalogue holds can be
// asserted on exactly what a translator edits.
namespace cube::i18n_test {

struct Entry {
    QString id;
    QString source;
    QString note;
    // One form, or the language's plural forms in order.
    QStringList forms;
    bool plural = false;
    // "unfinished", "vanished", "obsolete", or empty.
    QString type;
};

struct Catalogue {
    QString path;
    QString language;  // the <TS language=...> attribute
    std::vector<Entry> entries;

    [[nodiscard]] const Entry* find(const QString& id) const;
};

// One string of docs/design/strings.json.
struct Source {
    QString id;
    QString note;
    bool plural = false;
    // The one text, or {one, other}.
    QStringList forms;
};

// A language of src/app/i18n/languages.json.
struct Language {
    QString code;
    QString file;
    QString tag;
    int plurals = 2;
    bool rtl = false;
    bool translated = false;
    // A complete first translation that no fluent reader has been through.
    bool draft = false;
    bool pseudo = false;
};

[[nodiscard]] Catalogue read_catalogue(const QString& path);
[[nodiscard]] std::vector<Source> read_strings();
[[nodiscard]] std::vector<Language> read_languages();
[[nodiscard]] QString catalogue_path(const Language& language);

// The %1..%9 placeholders of `text`, sorted.
[[nodiscard]] QStringList argument_placeholders(const QString& text);
// Whether `text` carries the plural count placeholder, %n or %Ln.
[[nodiscard]] bool has_count_placeholder(const QString& text);
// strings.json writes the count %n; the catalogues write it %Ln.
[[nodiscard]] QString locale_count(QString text);

}  // namespace cube::i18n_test
