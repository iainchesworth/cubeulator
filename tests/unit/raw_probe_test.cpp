// Scratch diagnostic (NOT for commit): measures how many stickers of a
// corpus clip are TRULY misread by classify_lab, before resolve_colors
// forces a 9-per-colour census. The committed [.assembly-probe] searches
// only census-PRESERVING edits over the already-resolved faces; that hides
// a misclassification the resolver "fixed" by moving the census correction
// onto the wrong cell. Here we classify the raw per-cell Lab directly and
// search the minimal ARBITRARY relabels (distance 1..3) that make the real
// oracle assemble, using the shipping assembler verbatim.

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using cube::model::Facelet;
using cube::model::FacetColor;

char letter(FacetColor c) {
    switch (c) {
        case FacetColor::white:
            return 'W';
        case FacetColor::yellow:
            return 'Y';
        case FacetColor::red:
            return 'R';
        case FacetColor::orange:
            return 'O';
        case FacetColor::green:
            return 'G';
        case FacetColor::blue:
            return 'B';
        case FacetColor::unknown:
            return '?';
    }
    return '?';
}

constexpr std::array<FacetColor, 6> kColors{FacetColor::white, FacetColor::yellow,
                                            FacetColor::red,   FacetColor::orange,
                                            FacetColor::green, FacetColor::blue};

using Faces = std::array<std::array<Facelet, 9>, 6>;

bool assembles(const Faces& f) {
    return cube::solver::assemble_scanned_faces_with_mapping(f).has_value();
}

std::array<int, 7> census(const Faces& f) {
    std::array<int, 7> c{};
    for (auto& face : f) {
        for (auto& fac : face)
            ++c[static_cast<std::size_t>(fac.color)];
    }
    return c;
}

// enum: unknown=0, white=1, yellow=2, red=3, orange=4, green=5, blue=6
bool census_ok(const Faces& f) {
    auto c = census(f);
    if (c[0] != 0)
        return false;  // no unknowns
    for (std::size_t i = 1; i <= 6; ++i) {
        if (c[i] != 9)
            return false;
    }
    return true;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
        out.push_back(field);
    return out;
}

struct SlotRow {
    int slot = -1;
    char store_face = '?';
    std::array<cv::Vec3f, 9> lab{};
};

std::vector<SlotRow> read_slot_rows(const std::string& path) {
    std::vector<SlotRow> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("slot,", 0) != 0)
            continue;
        const auto f = split_csv(line);
        if (f.size() < 46)
            continue;
        SlotRow r;
        r.store_face = f[6].empty() ? '?' : f[6][0];
        r.slot = std::atoi(f[7].c_str());
        for (std::size_t c = 0; c < 9; ++c) {
            const std::size_t b = 19 + c * 3;
            r.lab[c] = cv::Vec3f(static_cast<float>(std::atof(f[b].c_str())),
                                 static_cast<float>(std::atof(f[b + 1].c_str())),
                                 static_cast<float>(std::atof(f[b + 2].c_str())));
        }
        rows.push_back(r);
    }
    return rows;
}

}  // namespace

FacetColor color_of(char ch) {
    switch (ch) {
        case 'W':
            return FacetColor::white;
        case 'Y':
            return FacetColor::yellow;
        case 'R':
            return FacetColor::red;
        case 'O':
            return FacetColor::orange;
        case 'G':
            return FacetColor::green;
        case 'B':
            return FacetColor::blue;
        default:
            return FacetColor::unknown;
    }
}

// Assemble six explicit 9-letter face strings (";"-separated in CUBE_ASM_FACES),
// through the shipping oracle. Lets any hypothesis cube be tested at distance 0
// without rebuilding.
// Hue-adjacent ("confusable") alternatives: the colour pairs a hue classifier
// can plausibly flip under varying illumination. If the minimal repair of an
// INVALID cube lies entirely within these, the fault is photometric colour;
// if it needs a far-apart flip (e.g. B<->R), geometry/fusion is implicated.
std::vector<FacetColor> confusable_alts(FacetColor c) {
    switch (c) {
        case FacetColor::white:
            return {FacetColor::yellow};
        case FacetColor::yellow:
            return {FacetColor::white, FacetColor::orange, FacetColor::green};
        case FacetColor::orange:
            return {FacetColor::red, FacetColor::yellow};
        case FacetColor::red:
            return {FacetColor::orange};
        case FacetColor::green:
            return {FacetColor::yellow, FacetColor::blue};
        case FacetColor::blue:
            return {FacetColor::green};
        default:
            return {};
    }
}

TEST_CASE("faces-probe confusable: minimal hue-adjacent repair of an INVALID cube",
          "[.faces-confusable]") {
    const char* env = std::getenv("CUBE_ASM_FACES");
    if (env == nullptr)
        SKIP("CUBE_ASM_FACES not set");
    std::string s(env);
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ';'))
        if (!tok.empty())
            parts.push_back(tok);
    REQUIRE(parts.size() == 6);
    Faces f{};
    for (std::size_t i = 0; i < 6; ++i)
        for (std::size_t c = 0; c < 9; ++c)
            f[i][c].color = color_of(parts[i][c]);
    if (assembles(f)) {
        std::printf("# confusable: already VALID\n");
        SUCCEED();
        return;
    }
    // distance 1..4, confusable-only, census-corrected.
    for (int K = 1; K <= 4; ++K) {
        std::vector<std::size_t> idx(static_cast<std::size_t>(K), 0);
        // simple recursive enumeration of K positions
        long checked = 0;
        int hits = 0;
        std::function<void(int, std::size_t)> rec = [&](int depth, std::size_t start) {
            if (hits > 0)
                return;
            if (depth == K) {
                // enumerate confusable alternatives for the K chosen cells
                std::vector<std::vector<FacetColor>> opts(static_cast<std::size_t>(K));
                for (int d = 0; d < K; ++d)
                    opts[static_cast<std::size_t>(d)] =
                        confusable_alts(f[idx[static_cast<std::size_t>(d)] / 9]
                                         [idx[static_cast<std::size_t>(d)] % 9]
                                             .color);
                std::vector<std::size_t> pick(static_cast<std::size_t>(K), 0);
                for (;;) {
                    Faces t = f;
                    for (int d = 0; d < K; ++d) {
                        const std::size_t p = idx[static_cast<std::size_t>(d)];
                        if (opts[static_cast<std::size_t>(d)].empty())
                            goto next;
                        t[p / 9][p % 9].color =
                            opts[static_cast<std::size_t>(d)][pick[static_cast<std::size_t>(d)]];
                    }
                    if (census_ok(t)) {
                        ++checked;
                        if (assembles(t)) {
                            std::printf("#   confusable d%d repair:", K);
                            for (int d = 0; d < K; ++d) {
                                const std::size_t p = idx[static_cast<std::size_t>(d)];
                                std::printf(" [slot=%zu cell=%zu %c->%c]", p / 9, p % 9,
                                            letter(f[p / 9][p % 9].color),
                                            letter(t[p / 9][p % 9].color));
                            }
                            std::printf("\n");
                            ++hits;
                        }
                    }
                next:;
                    // increment pick
                    int d = K - 1;
                    while (d >= 0) {
                        if (opts[static_cast<std::size_t>(d)].empty()) {
                            --d;
                            continue;
                        }
                        if (++pick[static_cast<std::size_t>(d)] <
                            opts[static_cast<std::size_t>(d)].size())
                            break;
                        pick[static_cast<std::size_t>(d)] = 0;
                        --d;
                    }
                    if (d < 0)
                        break;
                }
                return;
            }
            for (std::size_t p = start; p < 54; ++p) {
                idx[static_cast<std::size_t>(depth)] = p;
                rec(depth + 1, p + 1);
                if (hits > 0)
                    return;
            }
        };
        rec(0, 0);
        std::printf("# confusable: distance-%d hue-adjacent repairs = %d (census-checked=%ld)\n", K,
                    hits, checked);
        if (hits > 0)
            break;
    }
    SUCCEED();
}

TEST_CASE("faces-probe: assemble six explicit face strings", "[.faces-probe]") {
    const char* env = std::getenv("CUBE_ASM_FACES");
    if (env == nullptr)
        SKIP("CUBE_ASM_FACES not set");
    std::string s(env);
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ';'))
        if (!tok.empty())
            parts.push_back(tok);
    REQUIRE(parts.size() == 6);
    Faces f{};
    for (std::size_t i = 0; i < 6; ++i) {
        REQUIRE(parts[i].size() == 9);
        for (std::size_t c = 0; c < 9; ++c)
            f[i][c].color = color_of(parts[i][c]);
    }
    auto c = census(f);
    std::printf("# faces-probe: census ?%d W%d Y%d R%d O%d G%d B%d census9=%s assemble=%s\n", c[0],
                c[1], c[2], c[3], c[4], c[5], c[6], census_ok(f) ? "YES" : "NO",
                assembles(f) ? "VALID" : "INVALID");
    // If it does not assemble, search minimal ARBITRARY relabels (census-corrected).
    if (!assembles(f)) {
        int hits1 = 0;
        for (std::size_t p = 0; p < 54; ++p) {
            const FacetColor cur = f[p / 9][p % 9].color;
            for (const FacetColor alt : kColors) {
                if (alt == cur)
                    continue;
                Faces t = f;
                t[p / 9][p % 9].color = alt;
                if (census_ok(t) && assembles(t)) {
                    std::printf("#   d1 assembles: slot=%zu cell=%zu %c->%c\n", p / 9, p % 9,
                                letter(cur), letter(alt));
                    ++hits1;
                }
            }
        }
        std::printf("# faces-probe: distance-1 arbitrary relabels that assemble = %d\n", hits1);
        if (hits1 == 0) {
            int hits2 = 0;
            for (std::size_t p = 0; p < 54 && hits2 < 30; ++p) {
                const FacetColor cp = f[p / 9][p % 9].color;
                for (const FacetColor ap : kColors) {
                    if (ap == cp)
                        continue;
                    for (std::size_t q = p + 1; q < 54 && hits2 < 30; ++q) {
                        const FacetColor cq = f[q / 9][q % 9].color;
                        for (const FacetColor aq : kColors) {
                            if (aq == cq)
                                continue;
                            Faces t = f;
                            t[p / 9][p % 9].color = ap;
                            t[q / 9][q % 9].color = aq;
                            if (census_ok(t) && assembles(t)) {
                                std::printf(
                                    "#   d2 assembles: slot=%zu cell=%zu %c->%c ; slot=%zu "
                                    "cell=%zu %c->%c\n",
                                    p / 9, p % 9, letter(cp), letter(ap), q / 9, q % 9, letter(cq),
                                    letter(aq));
                                ++hits2;
                            }
                        }
                    }
                }
            }
            std::printf("# faces-probe: distance-2 arbitrary relabels that assemble = %d\n", hits2);
            if (hits2 == 0) {
                int hits3 = 0;
                long checked = 0;
                for (std::size_t p = 0; p < 54 && hits3 < 20; ++p) {
                    const FacetColor cp = f[p / 9][p % 9].color;
                    for (const FacetColor ap : kColors) {
                        if (ap == cp)
                            continue;
                        for (std::size_t q = p + 1; q < 54 && hits3 < 20; ++q) {
                            const FacetColor cq = f[q / 9][q % 9].color;
                            for (const FacetColor aq : kColors) {
                                if (aq == cq)
                                    continue;
                                for (std::size_t r = q + 1; r < 54 && hits3 < 20; ++r) {
                                    const FacetColor cr = f[r / 9][r % 9].color;
                                    for (const FacetColor ar : kColors) {
                                        if (ar == cr)
                                            continue;
                                        Faces t = f;
                                        t[p / 9][p % 9].color = ap;
                                        t[q / 9][q % 9].color = aq;
                                        t[r / 9][r % 9].color = ar;
                                        if (!census_ok(t))
                                            continue;
                                        ++checked;
                                        if (assembles(t)) {
                                            std::printf(
                                                "#   d3: slot=%zu cell=%zu %c->%c ; slot=%zu "
                                                "cell=%zu %c->%c ; "
                                                "slot=%zu cell=%zu %c->%c\n",
                                                p / 9, p % 9, letter(cp), letter(ap), q / 9, q % 9,
                                                letter(cq), letter(aq), r / 9, r % 9, letter(cr),
                                                letter(ar));
                                            ++hits3;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                std::printf(
                    "# faces-probe: distance-3 arbitrary relabels that assemble = %d "
                    "(checked=%ld)\n",
                    hits3, checked);
            }
        }
    }
    SUCCEED();
}

TEST_CASE("raw-probe: minimal arbitrary relabels of the RAW classified faces", "[.raw-probe]") {
    const char* dump = std::getenv("CUBE_ASM_DUMP");
    if (dump == nullptr)
        SKIP("CUBE_ASM_DUMP not set");
    const auto rows = read_slot_rows(dump);
    if (rows.size() != 6)
        SKIP("need 6 slot rows, got " << rows.size());

    std::array<cube::vision::CapturedFace, 6> captured{};
    std::array<char, 6> slot_face{};
    for (const auto& row : rows) {
        if (row.slot < 0 || row.slot >= 6)
            continue;
        captured[static_cast<std::size_t>(row.slot)].lab = row.lab;
        slot_face[static_cast<std::size_t>(row.slot)] = row.store_face;
    }

    // RAW classification, cell by cell.
    Faces raw{};
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t c = 0; c < 9; ++c) {
            raw[f][c] = cube::vision::classify_lab(captured[f].lab[c]);
        }
    }
    std::printf("# raw-probe: RAW classified faces (slot:store):\n");
    for (std::size_t f = 0; f < 6; ++f) {
        std::string s;
        for (auto& fac : raw[f])
            s += letter(fac.color);
        std::printf("#   slot=%zu face=%c %s\n", f, slot_face[f], s.c_str());
    }
    auto rc = census(raw);
    std::printf(
        "# raw-probe: raw census ?%d W%d Y%d R%d O%d G%d B%d  base-assembles=%s census9=%s\n",
        rc[0], rc[1], rc[2], rc[3], rc[4], rc[5], rc[6], assembles(raw) ? "YES" : "NO",
        census_ok(raw) ? "YES" : "NO");

    // Distance-1..3 arbitrary relabels, census pre-filtered (assemble needs 9-each).
    const auto try_report = [&](const Faces& trial, const std::string& desc) {
        if (!census_ok(trial))
            return false;
        if (assembles(trial)) {
            std::printf("#   ASSEMBLES via %s\n", desc.c_str());
            return true;
        }
        return false;
    };

    // distance 1
    int hits1 = 0;
    for (std::size_t p = 0; p < 54; ++p) {
        const FacetColor cur = raw[p / 9][p % 9].color;
        for (const FacetColor alt : kColors) {
            if (alt == cur)
                continue;
            Faces t = raw;
            t[p / 9][p % 9].color = alt;
            char d[96];
            std::snprintf(d, sizeof(d), "d1: slot=%zu(%c) cell=%zu %c->%c", p / 9, slot_face[p / 9],
                          p % 9, letter(cur), letter(alt));
            if (try_report(t, d))
                ++hits1;
        }
    }
    std::printf("# raw-probe: distance-1 arbitrary relabels that assemble = %d\n", hits1);

    if (hits1 == 0) {
        int hits2 = 0;
        for (std::size_t p = 0; p < 54 && hits2 < 40; ++p) {
            const FacetColor cp = raw[p / 9][p % 9].color;
            for (const FacetColor ap : kColors) {
                if (ap == cp)
                    continue;
                for (std::size_t q = p + 1; q < 54 && hits2 < 40; ++q) {
                    const FacetColor cq = raw[q / 9][q % 9].color;
                    for (const FacetColor aq : kColors) {
                        if (aq == cq)
                            continue;
                        Faces t = raw;
                        t[p / 9][p % 9].color = ap;
                        t[q / 9][q % 9].color = aq;
                        char d[160];
                        std::snprintf(
                            d, sizeof(d),
                            "d2: slot=%zu(%c)cell=%zu %c->%c ; slot=%zu(%c)cell=%zu %c->%c", p / 9,
                            slot_face[p / 9], p % 9, letter(cp), letter(ap), q / 9,
                            slot_face[q / 9], q % 9, letter(cq), letter(aq));
                        if (try_report(t, d))
                            ++hits2;
                    }
                }
            }
        }
        std::printf("# raw-probe: distance-2 arbitrary relabels that assemble = %d\n", hits2);

        if (hits2 == 0) {
            long checked = 0;
            int hits3 = 0;
            for (std::size_t p = 0; p < 54 && hits3 < 20; ++p) {
                const FacetColor cp = raw[p / 9][p % 9].color;
                for (const FacetColor ap : kColors) {
                    if (ap == cp)
                        continue;
                    for (std::size_t q = p + 1; q < 54 && hits3 < 20; ++q) {
                        const FacetColor cq = raw[q / 9][q % 9].color;
                        for (const FacetColor aq : kColors) {
                            if (aq == cq)
                                continue;
                            for (std::size_t r = q + 1; r < 54 && hits3 < 20; ++r) {
                                const FacetColor cr = raw[r / 9][r % 9].color;
                                for (const FacetColor ar : kColors) {
                                    if (ar == cr)
                                        continue;
                                    Faces t = raw;
                                    t[p / 9][p % 9].color = ap;
                                    t[q / 9][q % 9].color = aq;
                                    t[r / 9][r % 9].color = ar;
                                    if (!census_ok(t))
                                        continue;
                                    ++checked;
                                    if (assembles(t)) {
                                        std::printf(
                                            "#   d3 ASSEMBLES: slot=%zu cell=%zu %c->%c ; slot=%zu "
                                            "cell=%zu "
                                            "%c->%c ; slot=%zu cell=%zu %c->%c\n",
                                            p / 9, p % 9, letter(cp), letter(ap), q / 9, q % 9,
                                            letter(cq), letter(aq), r / 9, r % 9, letter(cr),
                                            letter(ar));
                                        ++hits3;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            std::printf(
                "# raw-probe: distance-3 arbitrary relabels that assemble = %d "
                "(census9-checked=%ld)\n",
                hits3, checked);
        }
    }
    CHECK(rows.size() == 6);
}
