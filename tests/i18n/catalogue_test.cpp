// The translation catalogues, read as the files a translator edits.
//
// These are the family's translation gates (ICL Forge's test_translations.cpp:
// nothing empty, placeholders kept, brand terms kept, the source sets match,
// nothing unfinished), adapted to ids, and held to strings.json, which is
// where the English and the notes live. docs/i18n.md says what each one is
// for and what to do when it fails.
#include "catalogue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace cube::i18n_test;

namespace {

// Words that name the product. They read the same in every language, and a
// translation that has lost one has renamed something.
const QStringList kBrandTerms{QStringLiteral("Cubeulator")};

[[nodiscard]] std::string str(const QString& s) {
    return s.toStdString();
}

[[nodiscard]] std::vector<Catalogue> all_catalogues() {
    std::vector<Catalogue> out;
    for (const Language& l : read_languages()) {
        out.push_back(read_catalogue(catalogue_path(l)));
    }
    return out;
}

}  // namespace

TEST_CASE("every language has a catalogue and the pseudo-locale has one", "[i18n][catalogue]") {
    const auto languages = read_languages();
    REQUIRE(languages.size() == 10);  // nine languages and the pseudo-locale
    REQUIRE(languages.front().code == QLatin1String("en"));
    REQUIRE(languages.back().pseudo);
    for (const Language& l : languages) {
        INFO(str(l.code));
        const Catalogue c = read_catalogue(catalogue_path(l));
        CHECK_FALSE(c.entries.empty());
    }
}

TEST_CASE("every catalogue holds exactly the ids of strings.json", "[i18n][catalogue]") {
    const auto strings = read_strings();
    REQUIRE(strings.size() > 300);
    QSet<QString> expected;
    for (const Source& s : strings) {
        expected.insert(s.id);
    }
    for (const Catalogue& c : all_catalogues()) {
        QSet<QString> have;
        for (const Entry& e : c.entries) {
            INFO(str(c.language) << ": " << str(e.id));
            CHECK_FALSE(have.contains(e.id));  // an id is written once
            have.insert(e.id);
        }
        INFO("catalogue " << str(c.path));
        for (const QString& id : expected - have) {
            FAIL_CHECK("missing id " << str(id) << " (run tools/i18n/strings_to_ts.py --write)");
        }
        for (const QString& id : have - expected) {
            FAIL_CHECK(
                "id " << str(id)
                      << " is not in strings.json (run tools/i18n/strings_to_ts.py --write)");
        }
    }
}

TEST_CASE("the English catalogue is strings.json, notes and plural forms included",
          "[i18n][catalogue]") {
    const Catalogue en = read_catalogue(catalogue_path(read_languages().front()));
    for (const Source& s : read_strings()) {
        const Entry* e = en.find(s.id);
        INFO(str(s.id));
        REQUIRE(e != nullptr);
        CHECK(e->note == s.note);
        CHECK(e->plural == s.plural);
        QStringList expected;
        for (const QString& f : s.forms) {
            expected.append(locale_count(f));
        }
        CHECK(e->forms == expected);
        CHECK(e->source == expected.back());
        CHECK(e->type.isEmpty());
    }
}

TEST_CASE("a plural message has exactly the number of forms its language has",
          "[i18n][catalogue][plural]") {
    const auto languages = read_languages();
    for (const Language& l : languages) {
        const Catalogue c = read_catalogue(catalogue_path(l));
        int plural_messages = 0;
        for (const Entry& e : c.entries) {
            INFO(str(l.code) << ": " << str(e.id));
            if (e.plural) {
                ++plural_messages;
                CHECK(e.forms.size() == l.plurals);
            } else {
                CHECK(e.forms.size() == 1);
            }
        }
        CHECK(plural_messages == 8);
    }
}

TEST_CASE("nothing is empty, unfinished or obsolete where the language says it is done",
          "[i18n][catalogue]") {
    const auto languages = read_languages();
    for (const Language& l : languages) {
        const Catalogue c = read_catalogue(catalogue_path(l));
        int unfinished = 0;
        for (const Entry& e : c.entries) {
            const bool empty = std::any_of(e.forms.begin(), e.forms.end(),
                                           [](const QString& f) { return f.trimmed().isEmpty(); });
            INFO(str(l.code) << ": " << str(e.id));
            CHECK((e.type != QLatin1String("vanished") && e.type != QLatin1String("obsolete")));
            // An entry that claims to be finished has text in every form.
            if (e.type.isEmpty()) {
                CHECK_FALSE(empty);
            } else {
                ++unfinished;
            }
        }
        // English and the pseudo-locale are generated: always whole. Another
        // language is whole once E5 has translated it and languages.json says
        // so; until then its unfinished entries are what falls back to English.
        // A draft (`"draft": true`: ja, zh-CN) is a whole first translation
        // no fluent reader has been through: it is not held to this, so that a
        // string added since falls back instead of failing every lane's tests.
        if (l.translated) {
            INFO(str(l.code) << " is marked translated and has " << unfinished
                             << " unfinished entries");
            CHECK(unfinished == 0);
        }
    }
}

TEST_CASE("placeholders survive translation in every catalogue", "[i18n][catalogue]") {
    for (const Catalogue& c : all_catalogues()) {
        for (const Entry& e : c.entries) {
            if (!e.type.isEmpty()) {
                continue;
            }
            // The arguments of the English, in every form of every language.
            const QStringList wanted = argument_placeholders(e.source);
            for (const QString& form : e.forms) {
                INFO(str(c.language) << " / " << str(e.id) << ": " << str(form));
                CHECK(argument_placeholders(form) == wanted);
            }
            // The count: the form that is the general plural keeps it, where
            // the source's does. (A language's singular may say "one" in
            // words, as the English "The side read so far" does.)
            if (e.plural && has_count_placeholder(e.source)) {
                INFO(str(c.language) << " / " << str(e.id));
                CHECK(has_count_placeholder(e.forms.back()));
            }
        }
    }
}

TEST_CASE("brand terms survive translation in every catalogue", "[i18n][catalogue]") {
    for (const Catalogue& c : all_catalogues()) {
        for (const Entry& e : c.entries) {
            if (!e.type.isEmpty()) {
                continue;
            }
            for (const QString& term : kBrandTerms) {
                if (!e.source.contains(term)) {
                    continue;
                }
                for (const QString& form : e.forms) {
                    INFO(str(c.language)
                         << " / " << str(e.id) << " [" << str(term) << "]: " << str(form));
                    CHECK(form.contains(term));
                }
            }
        }
    }
    // The product name is the one string whose translation is its source.
    // (The pseudo-locale brackets it, as it brackets everything, so that a
    // name that did not go through an id lookup can be told from one that did.)
    for (const Catalogue& c : all_catalogues()) {
        if (c.language == QLatin1String("en_XA")) {
            continue;
        }
        const Entry* name = c.find(QStringLiteral("app.name"));
        REQUIRE(name != nullptr);
        if (name->type.isEmpty()) {
            CHECK(name->forms.front() == QLatin1String("Cubeulator"));
        }
    }
}

TEST_CASE("the pseudo-locale is longer, accented and bracketed", "[i18n][catalogue][pseudo]") {
    const auto languages = read_languages();
    const Language& pseudo = languages.back();
    REQUIRE(pseudo.pseudo);
    const Catalogue xx = read_catalogue(catalogue_path(pseudo));
    const Catalogue en = read_catalogue(catalogue_path(languages.front()));
    REQUIRE(xx.entries.size() == en.entries.size());
    for (const Entry& e : xx.entries) {
        const Entry* english = en.find(e.id);
        REQUIRE(english != nullptr);
        REQUIRE(e.forms.size() == english->forms.size());
        for (qsizetype i = 0; i < e.forms.size(); ++i) {
            const QString& pseudo_text = e.forms.at(i);
            const QString& plain = english->forms.at(i);
            INFO(str(e.id) << ": " << str(pseudo_text));
            REQUIRE(pseudo_text.startsWith(QLatin1Char('[')));
            REQUIRE(pseudo_text.endsWith(QLatin1Char(']')));
            // content.md allows a translation 40% more than the English; the
            // pseudo-locale takes all of it, and the brackets besides.
            CHECK(pseudo_text.size() >=
                  plain.size() + static_cast<qsizetype>(static_cast<double>(plain.size()) * 0.4) -
                      2);
            CHECK(argument_placeholders(pseudo_text) == argument_placeholders(plain));
            // Nothing is left looking untouched: a plain ASCII vowel
            // surviving outside a placeholder would be a string that was not
            // run through the transform.
            QString visible = pseudo_text;
            visible.remove(QRegularExpression(QStringLiteral("%L?[0-9n]+|<[^>]*>|Cubeulator")));
            CHECK_FALSE(visible.contains(QRegularExpression(QStringLiteral("[aeiouAEIOU]"))));
        }
    }
}

TEST_CASE("a catalogue is not marked right to left unless its language is", "[i18n][catalogue]") {
    // Hebrew, Arabic and Yiddish: three of the nine.
    int rtl = 0;
    for (const Language& l : read_languages()) {
        rtl += l.rtl ? 1 : 0;
    }
    CHECK(rtl == 3);
}

namespace {

// The characters a right-to-left catalogue may use: its own script's letters
// and points, the Latin of the product name, notation and keys, and the few
// marks the app's wording uses. Every one is in the face the browser build
// draws it with (Qt's bundled DejaVu Sans) and in the system's own faces.
// What it refuses is what a keyboard or a paste brings in by accident: Persian
// letters in Arabic, Hebrew presentation forms (the generator writes letter
// and point instead), a Cyrillic lookalike.
[[nodiscard]] bool allowed_in_rtl_catalogue(const QString& code, char32_t c) {
    if (c == U'\n' || (c >= 0x20 && c < 0x7F)) {
        return true;  // layout, Latin, digits, ASCII punctuation
    }
    if (c == 0x00B7 || c == 0x00D7 || c == 0x2026 || c == 0x200E || c == 0x200F) {
        // middle dot, multiplication sign (a size of cube, 5×5), ellipsis,
        // left-to-right and right-to-left marks
        return true;
    }
    if (code == QLatin1String("ar")) {
        return (c >= 0x0621 && c <= 0x063A) || (c >= 0x0640 && c <= 0x064A) ||  // letters, tatweel
               (c >= 0x064B && c <= 0x0652) ||                                  // tashkeel
               (c >= 0x0660 && c <= 0x0669) ||  // Arabic-Indic digits, as `%Ln` shows counts
               c == 0x060C || c == 0x061B || c == 0x061F;  // comma, semicolon, question mark
    }
    // Hebrew and Yiddish: letters, the points, maqaf, geresh and gershayim,
    // and the three Yiddish digraphs.
    return (c >= 0x05D0 && c <= 0x05EA) || (c >= 0x05B0 && c <= 0x05BF) ||
           (c >= 0x05C1 && c <= 0x05C2) || (c >= 0x05F0 && c <= 0x05F4);
}

}  // namespace

TEST_CASE("a right-to-left catalogue uses characters of its own script and the app's marks only",
          "[i18n][catalogue][script]") {
    for (const Language& l : read_languages()) {
        if (!l.rtl) {
            continue;
        }
        const Catalogue c = read_catalogue(catalogue_path(l));
        for (const Entry& e : c.entries) {
            if (!e.type.isEmpty()) {
                continue;
            }
            for (const QString& form : e.forms) {
                for (const char32_t ch : form.toUcs4()) {
                    INFO(str(l.code)
                         << " / " << str(e.id) << ": U+"
                         << QString::number(static_cast<uint>(ch), 16).toUpper().toStdString()
                         << " in " << str(form));
                    CHECK(allowed_in_rtl_catalogue(l.code, ch));
                }
            }
        }
    }
}

TEST_CASE("a right-to-left string that opens with a Latin word opens with a right-to-left mark",
          "[i18n][catalogue][bidi]") {
    // Qt Quick Text takes a string's direction from its first strong
    // character. "Cubeulator ..." with nothing before it is laid out left to
    // right: the words come out in the wrong order and the text sits against
    // the wrong edge (seen in the browser build). U+200F first fixes it. The
    // product name on its own (app.name, the first row of the licences) is a
    // name and not a sentence.
    const QRegularExpression placeholder(QStringLiteral("%L?[0-9n]+"));
    for (const Language& l : read_languages()) {
        if (!l.rtl) {
            continue;
        }
        const Catalogue c = read_catalogue(catalogue_path(l));
        for (const Entry& e : c.entries) {
            if (!e.type.isEmpty() || e.id == QLatin1String("app.name") ||
                e.id == QLatin1String("licences.own")) {
                continue;
            }
            for (const QString& form : e.forms) {
                QString text = form;
                text.remove(placeholder);
                for (const QChar ch : text) {
                    if (!ch.isLetter()) {
                        continue;
                    }
                    INFO(str(l.code) << " / " << str(e.id) << ": " << str(form));
                    if (ch.unicode() < 0x80) {
                        CHECK(form.startsWith(QChar(0x200F)));
                    }
                    break;
                }
            }
        }
    }
}
