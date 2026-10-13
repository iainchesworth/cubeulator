// Assembly probe - a hidden DIAGNOSTIC harness, never part of the normal
// suite. It answers one question the [.cross-session] verdict raises but
// cannot itself localise: when a corpus clip resolves to a perfect
// 9/9/9/9/9/9 census yet assemble_scanned_faces_with_mapping() still returns
// INVALID, WHICH sticker is wrong, and is that sticker's RAW colour actually
// closer to the label the oracle wants - a fixable classifier error - or is
// the classifier right and the oracle only wants it moved because the true
// error is a different, unpinnable sticker (in which case a classify_lab
// change is chasing the wrong cell).
//
// Method, deliberately using the SHIPPING oracles verbatim so a fault this
// probe reports is a fault the product has, not an artefact of a
// re-derivation:
//   - cube::vision::resolve_colors() over the six captured faces (the same
//     call finalizeIfComplete makes), then
//   - cube::solver::assemble_scanned_faces_with_mapping() (which succeeds
//     only when some rotation combination builds a Cube::from_facelets() that
//     is_solvable() confirms).
// Neither is reimplemented here.
//
// Two entry points:
//   1. CONTROL (always runs, needs no corpus): a solved cube must assemble at
//      relabel-distance 0, and a solved cube with one sticker corrupted must
//      be repaired by exactly the distance-1 relabel that restores it. Until
//      those two hold, no live5 result from this probe is trustworthy.
//   2. LIVE (runs when CUBE_ASM_DUMP names a tee written by the
//      [.cross-session] harness): parse the six `slot` rows' RAW per-cell Lab,
//      feed them through the real resolver and assembler, confirm the INVALID
//      verdict, then search the minimal relabellings that could rescue it:
//        - the literal single-sticker relabel (54 x 5 = 270), reported WITH
//          its physically-buildable count, since a full-census base admits
//          none (one relabel makes the census 8/10, which is_physically_valid
//          rejects before solvability is consulted - so this search is
//          vacuous by construction and its emptiness proves nothing alone);
//        - the census-PRESERVING two-sticker SWAP (the true minimal edit);
//        - the census-preserving 3-CYCLE, when no swap works.
//      For every relabel that assembles, cross-reference the RAW Lab of each
//      moved cell (its L, chroma, hue and Lab distance to the session's red /
//      orange / yellow centres) so a reader can tell a fixable
//      misclassification from an oracle merely redistributing an error it
//      cannot pin.
//
// std::getenv is fine here (single-threaded, read-only); silence MSVC's CRT
// deprecation the way the other offline harnesses do.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using cube::model::Facelet;
using cube::model::FacetColor;

char color_letter(FacetColor color) {
    switch (color) {
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

// The six real sticker colours, in the order this probe iterates alternatives.
constexpr std::array<FacetColor, 6> kColors{FacetColor::white, FacetColor::yellow,
                                            FacetColor::red,   FacetColor::orange,
                                            FacetColor::green, FacetColor::blue};

using ResolvedFaces = std::array<std::array<Facelet, 9>, 6>;

[[nodiscard]] bool assembles(const ResolvedFaces& faces) {
    return cube::solver::assemble_scanned_faces_with_mapping(faces).has_value();
}

// A single-sticker relabelling that made the cube assemble.
struct SingleRelabel {
    std::size_t face = 0;  // input slot index 0..5
    std::size_t cell = 0;  // 0..8, row-major
    FacetColor from = FacetColor::unknown;
    FacetColor to = FacetColor::unknown;
};

[[nodiscard]] std::vector<SingleRelabel> single_relabels_that_assemble(const ResolvedFaces& base) {
    std::vector<SingleRelabel> hits;
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t c = 0; c < 9; ++c) {
            const FacetColor original = base[f][c].color;
            for (const FacetColor alt : kColors) {
                if (alt == original) {
                    continue;
                }
                ResolvedFaces trial = base;
                trial[f][c].color = alt;
                if (assembles(trial)) {
                    hits.push_back(SingleRelabel{f, c, original, alt});
                }
            }
        }
    }
    return hits;
}

// A census-PRESERVING edit is the only kind that can ever assemble a
// resolved cube. Because is_physically_valid() requires exactly nine of each
// colour, any single relabel of a 9/9/9/9/9/9 resolved cube turns the census
// into 8/10 and is rejected before solvability is even consulted - so the
// single-relabel search above is guaranteed empty for a full-census input and
// proves nothing on its own. The minimal edit that keeps the census is a
// two-sticker SWAP (exchange the labels of two differently-coloured cells);
// the next is a 3-CYCLE (rotate three labels among three colours). These
// searches are the real distance probe.

// Counts how many of the 270 single relabels are even physically buildable
// (they cannot be, for a full-census base) - measured, so the vacuousness is
// a number in the report rather than an assertion the reader has to take on
// trust.
[[nodiscard]] std::size_t single_relabels_physically_valid(const ResolvedFaces& base) {
    std::size_t valid = 0;
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t c = 0; c < 9; ++c) {
            const FacetColor original = base[f][c].color;
            for (const FacetColor alt : kColors) {
                if (alt == original) {
                    continue;
                }
                ResolvedFaces trial = base;
                trial[f][c].color = alt;
                // A census-broken cube can still nominally "assemble" only if
                // some rotation restores nine-each, which a single relabel
                // never does; assembles() therefore doubles as the physical
                // filter here.
                if (assembles(trial)) {
                    ++valid;
                }
            }
        }
    }
    return valid;
}

struct Swap {
    std::size_t pa = 0;  // flat position 0..53
    std::size_t pb = 0;
};

// Every census-preserving two-sticker swap that assembles.
[[nodiscard]] std::vector<Swap> swaps_that_assemble(const ResolvedFaces& base) {
    std::vector<Swap> hits;
    for (std::size_t p = 0; p < 54; ++p) {
        const FacetColor cp = base[p / 9][p % 9].color;
        for (std::size_t q = p + 1; q < 54; ++q) {
            const FacetColor cq = base[q / 9][q % 9].color;
            if (cp == cq) {
                continue;  // swapping like for like is a no-op
            }
            ResolvedFaces trial = base;
            trial[p / 9][p % 9].color = cq;
            trial[q / 9][q % 9].color = cp;
            if (assembles(trial)) {
                hits.push_back(Swap{p, q});
            }
        }
    }
    return hits;
}

struct Cycle3 {
    std::array<std::size_t, 3> pos{};  // flat positions, labels rotate pos0->pos1->pos2->pos0
};

// Every census-preserving 3-cycle of labels that assembles. A 3-cycle changes
// three stickers among three distinct colours; it is the next-smallest edit
// after a swap and the smallest that can fix a single mis-partitioned triple
// the resolver's exactly-9 constraint smeared across three clusters.
[[nodiscard]] std::vector<Cycle3> cycles_that_assemble(const ResolvedFaces& base, std::size_t cap) {
    std::vector<Cycle3> hits;
    for (std::size_t p = 0; p < 54; ++p) {
        const FacetColor cp = base[p / 9][p % 9].color;
        for (std::size_t q = p + 1; q < 54; ++q) {
            const FacetColor cq = base[q / 9][q % 9].color;
            for (std::size_t r = q + 1; r < 54; ++r) {
                const FacetColor cr = base[r / 9][r % 9].color;
                // Need three DISTINCT colours for a genuine 3-cycle; a repeated
                // colour degenerates to a swap already covered above.
                if (cp == cq || cq == cr || cp == cr) {
                    continue;
                }
                // Two rotation directions.
                for (int dir = 0; dir < 2; ++dir) {
                    ResolvedFaces trial = base;
                    if (dir == 0) {
                        trial[p / 9][p % 9].color = cr;
                        trial[q / 9][q % 9].color = cp;
                        trial[r / 9][r % 9].color = cq;
                    } else {
                        trial[p / 9][p % 9].color = cq;
                        trial[q / 9][q % 9].color = cr;
                        trial[r / 9][r % 9].color = cp;
                    }
                    if (assembles(trial)) {
                        hits.push_back(Cycle3{{p, q, r}});
                        if (hits.size() >= cap) {
                            return hits;
                        }
                    }
                }
            }
        }
    }
    return hits;
}

// ---- Live dump parsing ----------------------------------------------------

struct SlotRow {
    int slot = -1;
    char store_face = '?';
    std::array<cv::Vec3f, 9> lab{};
};

[[nodiscard]] std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        out.push_back(field);
    }
    return out;
}

// Reads the `slot` rows the [.cross-session] harness emits. Schema (its own
// header line): kind,clip,pass,t_ms,epoch,epoch_face,store_face,slot,outcome,
// contrib_obs,corner_fed,frontal_deg,pass_rot_deg,motion,belief_margin,
// certain,certain_face,convergence,colors,L0,a0,b0,...,L8,a8,b8 - so field 6
// is store_face, field 7 is the slot index, and fields 19..45 are the nine
// raw Labs in the same row-major solver order resolve_colors expects.
[[nodiscard]] std::vector<SlotRow> read_slot_rows(const std::string& path) {
    std::vector<SlotRow> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("slot,", 0) != 0) {
            continue;
        }
        const auto fields = split_csv(line);
        if (fields.size() < 46) {
            continue;
        }
        SlotRow row;
        row.store_face = fields[6].empty() ? '?' : fields[6][0];
        row.slot = std::atoi(fields[7].c_str());
        for (std::size_t c = 0; c < 9; ++c) {
            const std::size_t base = 19 + c * 3;
            row.lab[c] = cv::Vec3f(static_cast<float>(std::atof(fields[base].c_str())),
                                   static_cast<float>(std::atof(fields[base + 1].c_str())),
                                   static_cast<float>(std::atof(fields[base + 2].c_str())));
        }
        rows.push_back(row);
    }
    return rows;
}

struct Photometry {
    float L = 0.0F;
    float chroma = 0.0F;
    float hue = 0.0F;
};

[[nodiscard]] Photometry photometry(const cv::Vec3f& lab) {
    Photometry p;
    p.L = lab[0];
    p.chroma = std::hypot(lab[1], lab[2]);
    float hue = std::atan2(lab[2], lab[1]) * 180.0F / std::numbers::pi_v<float>;
    if (hue < 0.0F) {
        hue += 360.0F;
    }
    p.hue = hue;
    return p;
}

[[nodiscard]] float lab_dist(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f d = a - b;
    return std::sqrt(d.dot(d));
}

}  // namespace

// ---------------------------------------------------------------------------
// CONTROL - runs with no corpus. Establishes that the probe's brute-force is
// measuring the real oracle correctly before any live claim is made.
// ---------------------------------------------------------------------------
TEST_CASE("assembly probe control: solved assembles at distance 0, one-corruption at distance 1",
          "[.assembly-probe]") {
    // A solved cube: six uniform faces, one per colour. Assembly keys each
    // face by its centre, so input order is irrelevant.
    ResolvedFaces solved{};
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t c = 0; c < 9; ++c) {
            solved[f][c] = Facelet{.color = kColors[f], .confidence = 1.0F};
        }
    }

    // Distance 0: the solved cube assembles.
    const bool solved_ok = assembles(solved);
    std::printf("# probe-control: solved assembles=%s (expect YES)\n", solved_ok ? "YES" : "NO");
    CHECK(solved_ok);

    // Corrupt one non-centre sticker: white face, cell 0, painted red. The
    // census is now W8 R10, which no rotation can make solvable.
    ResolvedFaces corrupted = solved;
    corrupted[0][0].color = FacetColor::red;
    const bool corrupted_ok = assembles(corrupted);
    std::printf("# probe-control: one-corruption assembles=%s (expect NO)\n",
                corrupted_ok ? "YES" : "NO");
    CHECK_FALSE(corrupted_ok);

    // Distance 1: the brute-force must find the restoring relabel (cell 0
    // back to white). It may find only that one; it must not miss it.
    const auto hits = single_relabels_that_assemble(corrupted);
    bool restore_found = false;
    for (const auto& hit : hits) {
        if (hit.face == 0 && hit.cell == 0 && hit.to == FacetColor::white) {
            restore_found = true;
        }
    }
    std::printf(
        "# probe-control: distance-1 relabels that assemble=%zu, restore(W@face0cell0)-found=%s\n",
        hits.size(), restore_found ? "YES" : "NO");
    CHECK(restore_found);
}

// ---------------------------------------------------------------------------
// LIVE - runs only when CUBE_ASM_DUMP names a [.cross-session] tee. Reports;
// attaches no pass/fail to the live numbers (a corpus clip failing to
// assemble is the state of the product, not a regression in this probe).
// ---------------------------------------------------------------------------
TEST_CASE("assembly probe live: localise the sticker that blocks a corpus clip",
          "[.assembly-probe]") {
    const char* dump = std::getenv("CUBE_ASM_DUMP");
    if (dump == nullptr) {
        SKIP("CUBE_ASM_DUMP not set - tee a [.cross-session] run and point this at it");
    }
    const auto rows = read_slot_rows(dump);
    if (rows.size() != 6) {
        SKIP("expected 6 slot rows in the dump, found " << rows.size());
    }

    // Order the captured faces by slot index, exactly as the harness fed
    // resolve_colors (captured[i] = router.slot_at(i)). raw_lab stays aligned
    // to `captured` so a resolved facelet's raw colour is one index away.
    std::array<cube::vision::CapturedFace, 6> captured{};
    std::array<char, 6> slot_face{};
    for (const auto& row : rows) {
        if (row.slot < 0 || row.slot >= 6) {
            continue;
        }
        captured[static_cast<std::size_t>(row.slot)].lab = row.lab;
        slot_face[static_cast<std::size_t>(row.slot)] = row.store_face;
    }

    const auto resolved_result = cube::vision::resolve_colors(captured);
    REQUIRE(resolved_result.has_value());
    const ResolvedFaces resolved = *resolved_result;

    // Echo the resolved faces this probe computed (cross-check against the
    // harness's own "# resolved:" lines) and the raw centre of each slot.
    std::printf("# probe-live: resolved faces (slot:storeface letters)\n");
    for (std::size_t f = 0; f < 6; ++f) {
        std::string letters;
        for (const auto& facelet : resolved[f]) {
            letters += color_letter(facelet.color);
        }
        std::printf("#   slot=%zu face=%c %s\n", f, slot_face[f], letters.c_str());
    }

    const bool base_ok = assembles(resolved);
    std::printf("# probe-live: base assemble=%s (INVALID is the state under investigation)\n",
                base_ok ? "VALID" : "INVALID");

    // Session red / orange / yellow centres, taken as the RAW centre Lab of
    // whichever slot the resolver named that colour. These are the anchors a
    // relabel must be judged against.
    std::optional<cv::Vec3f> centre_R;
    std::optional<cv::Vec3f> centre_O;
    std::optional<cv::Vec3f> centre_Y;
    for (std::size_t f = 0; f < 6; ++f) {
        const cv::Vec3f raw_centre = captured[f].lab[cube::model::center_facelet_index];
        switch (resolved[f][cube::model::center_facelet_index].color) {
            case FacetColor::red:
                centre_R = raw_centre;
                break;
            case FacetColor::orange:
                centre_O = raw_centre;
                break;
            case FacetColor::yellow:
                centre_Y = raw_centre;
                break;
            default:
                break;
        }
    }
    const auto centre_line = [](const char* name, const std::optional<cv::Vec3f>& c) {
        if (!c) {
            std::printf("# probe-live: centre %s = <absent>\n", name);
            return;
        }
        const auto p = photometry(*c);
        std::printf("# probe-live: centre %s raw Lab=(%.2f,%.2f,%.2f) chroma=%.2f hue=%.2f\n", name,
                    static_cast<double>((*c)[0]), static_cast<double>((*c)[1]),
                    static_cast<double>((*c)[2]), static_cast<double>(p.chroma),
                    static_cast<double>(p.hue));
    };
    centre_line("R", centre_R);
    centre_line("O", centre_O);
    centre_line("Y", centre_Y);

    // The decisive per-relabel report: what the oracle wants changed, and
    // whether the RAW colour of that cell supports the new label.
    const auto report_cross_reference = [&](const SingleRelabel& hit) {
        const cv::Vec3f raw = captured[hit.face].lab[hit.cell];
        const auto p = photometry(raw);
        const auto dist_to = [&](const std::optional<cv::Vec3f>& c) {
            return c ? lab_dist(raw, *c) : -1.0F;
        };
        std::printf(
            "#   relabel slot=%zu(face=%c) cell=%zu  %c->%c  raw Lab=(%.2f,%.2f,%.2f) chroma=%.2f "
            "hue=%.2f"
            "  d(R)=%.2f d(O)=%.2f d(Y)=%.2f\n",
            hit.face, slot_face[hit.face], hit.cell, color_letter(hit.from), color_letter(hit.to),
            static_cast<double>(raw[0]), static_cast<double>(raw[1]), static_cast<double>(raw[2]),
            static_cast<double>(p.chroma), static_cast<double>(p.hue),
            static_cast<double>(dist_to(centre_R)), static_cast<double>(dist_to(centre_O)),
            static_cast<double>(dist_to(centre_Y)));
    };

    // The literal single-sticker relabel search the brief names. Reported
    // WITH the physically-valid count so the reader sees WHY it is empty: a
    // full-census base admits no single relabel that even builds a cube.
    const auto singles = single_relabels_that_assemble(resolved);
    const auto singles_valid = single_relabels_physically_valid(resolved);
    std::printf(
        "# probe-live: single-sticker relabels: assemble=%zu physically-buildable=%zu of 270"
        " (0 buildable is structural: one relabel makes the census 8/10)\n",
        singles.size(), singles_valid);
    for (const auto& hit : singles) {
        report_cross_reference(hit);
    }

    // The real minimal-edit distance probe: census-preserving swaps, then
    // 3-cycles. Each reported relabel names the stickers the oracle would have
    // to move and cross-references their raw colour, so a fixable
    // misclassification is separable from an error the oracle cannot pin.
    const auto swaps = swaps_that_assemble(resolved);
    std::printf("# probe-live: census-preserving 2-swaps that assemble = %zu\n", swaps.size());
    for (const auto& sw : swaps) {
        const FacetColor ca = resolved[sw.pa / 9][sw.pa % 9].color;
        const FacetColor cb = resolved[sw.pb / 9][sw.pb % 9].color;
        std::printf("#   swap:\n");
        report_cross_reference(SingleRelabel{sw.pa / 9, sw.pa % 9, ca, cb});
        report_cross_reference(SingleRelabel{sw.pb / 9, sw.pb % 9, cb, ca});
    }

    if (swaps.empty()) {
        std::printf("# probe-live: no 2-swap assembles; running 3-cycle search (cap 32)\n");
        const auto cycles = cycles_that_assemble(resolved, 32);
        std::printf("# probe-live: census-preserving 3-cycles that assemble = %zu\n",
                    cycles.size());
        for (const auto& cyc : cycles) {
            std::printf("#   3-cycle:\n");
            for (const std::size_t pos : cyc.pos) {
                const FacetColor cur = resolved[pos / 9][pos % 9].color;
                report_cross_reference(SingleRelabel{pos / 9, pos % 9, cur, FacetColor::unknown});
            }
        }
        if (cycles.empty()) {
            std::printf(
                "# probe-live: no 3-cycle assembles either - the resolved colouring is at least "
                "TWO independent swaps (>=4 mislabels) from any solvable cube\n");
        }
    }

    CHECK(rows.size() == 6);
}
