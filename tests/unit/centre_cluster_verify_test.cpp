// Independent verification of the [.centre-cluster] measurement - hidden
// tag, diagnostic only, no production behaviour depends on any of it.
//
// The centre-clustering prototype reported that no clip assembles and
// concluded that per-sticker colour accuracy is the binding constraint.
// The headline reproduces, but three of its supporting arguments were
// never actually controlled, and each one is load-bearing for the
// conclusion:
//
//   1. "from_facelets = 0, so the stickers never form 26 legitimately
//      labelled cubies, and is_physically_valid is never even reached."
//      from_facelets IS is_physically_valid - it calls it and returns the
//      error when it fails. There is no separate earlier constraint, and
//      the check itself is much weaker than "26 real cubies": it tests a
//      9-of-each census plus "no corner or edge carries two opposite
//      colours". So the prototype both mis-stated the constraint and
//      understated how weak it is.
//
//   2. "Independent recordings of the same cube don't agree - 13-20 of 54
//      cells differ." That is 63-76% AGREEMENT, quoted without the chance
//      floor it has to beat. This file supplies the two controls that make
//      the number interpretable: live5, which is a DIFFERENT scramble and
//      therefore should agree only by chance, and a sticker-shuffle floor.
//
//   3. Nothing tested whether the capture cell order is MIRRORED relative
//      to what the solver's facelet indexing expects. The assembler
//      searches four rolls per face, and rolls are rotations - a
//      reflection is not reachable from any of the 4096 combinations. A
//      systematic mirror would therefore produce exactly the observed
//      symptom, zero survivors on every clip, with nothing wrong with the
//      colours at all. It has to be excluded before the colour conclusion
//      can stand.
//
// It also quantifies what a validity pass is WORTH, which the prototype
// asserted rather than measured: if a large fraction of arbitrary sticker
// sets can satisfy the check, then a clip that did assemble would not be
// evidence of a correct read, and the whole "let physical validity select
// the arrangement" premise fails even on clips that succeed.
//
//   cube_unit_tests.exe "[.cc-verify]"
//
// The consensus faces are pinned literals rather than recomputed from the
// CSV so this file is a check ON that computation and not a copy of it:
// they are transcribed from a byte-identical local reproduction of the
// prototype's own run, and the CSV is recorded from live hardware and does
// not ship with the repo.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

using cube::solver::Color;

// The prototype's per-clip consensus faces, one letter per sticker in
// screen row-major order, transcribed from a local re-run that reproduced
// its printed output exactly. live1..live4 are one physical scramble;
// live5 is a different one and is the control.
struct ClipFaces {
    const char* clip;
    std::array<const char*, 6> face;
};

constexpr std::array<ClipFaces, 5> kConsensus{{
    {"live1", {"GGGWGRYOO", "BBRWYORWG", "OBWRRWYOB", "OBYGOWYRB", "YWGBWWRRY", "BORGBYOYG"}},
    {"live2", {"WORGWGRYW", "RRGBYBBWR", "GOYYROWBO", "YGOOOBBWY", "GWYWGRYBO", "RYWOBGRGB"}},
    {"live3", {"GWOGWGOOG", "GWRRYWRBB", "YOOOOBBWW", "YBRRRBBYY", "RBYRGWYWG", "OYRYBGOGW"}},
    {"live4", {"RGBOWYGGY", "BBRWYORWG", "OBWRRWYOB", "OBYGOWORB", "OBYRGWYWG", "RYOYBGRGW"}},
    {"live5", {"OGBYWOGGR", "BBRWYORWG", "BOYWRRWBO", "BRYWOGYBO", "OBYRGWYWG", "RYRGBYWGO"}},
}};

Color color_of(char letter) noexcept {
    switch (letter) {
        case 'W':
            return Color::White;
        case 'Y':
            return Color::Yellow;
        case 'R':
            return Color::Red;
        case 'O':
            return Color::Orange;
        case 'G':
            return Color::Green;
        default:
            return Color::Blue;
    }
}

cube::model::FacetColor facet_of(Color color) noexcept {
    switch (color) {
        case Color::White:
            return cube::model::FacetColor::white;
        case Color::Yellow:
            return cube::model::FacetColor::yellow;
        case Color::Red:
            return cube::model::FacetColor::red;
        case Color::Orange:
            return cube::model::FacetColor::orange;
        case Color::Green:
            return cube::model::FacetColor::green;
        default:
            return cube::model::FacetColor::blue;
    }
}

using Face9 = std::array<Color, 9>;
using Cube6 = std::array<Face9, 6>;

// Clockwise quarter turn of a row-major 3x3 grid, the same generator the
// prototype and the production assembler both search over.
constexpr std::array<std::size_t, 9> kRot90Source{6, 3, 0, 7, 4, 1, 8, 5, 2};

Face9 rotate(const Face9& face, int turns) noexcept {
    Face9 out = face;
    for (int turn = 0; turn < turns; ++turn) {
        Face9 next{};
        for (std::size_t k = 0; k < 9; ++k) {
            next[k] = out[kRot90Source[k]];
        }
        out = next;
    }
    return out;
}

// Transpose - the reflection that is NOT reachable from any of the four
// rolls. Composed with the rolls it generates the other four elements of
// the square's symmetry group, so testing transpose alone (the assembler
// supplies the rolls) covers every mirrored reading.
Face9 mirror(const Face9& face) noexcept {
    constexpr std::array<std::size_t, 9> kTranspose{0, 3, 6, 1, 4, 7, 2, 5, 8};
    Face9 out{};
    for (std::size_t k = 0; k < 9; ++k) {
        out[k] = face[kTranspose[k]];
    }
    return out;
}

Cube6 faces_of(const ClipFaces& clip) noexcept {
    Cube6 out{};
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t k = 0; k < 9; ++k) {
            out[f][k] = color_of(clip.face[f][k]);
        }
    }
    return out;
}

// Order six free-form faces the way from_facelets indexes them, by the
// cube position each centre colour names. Returns false if the centres
// collide, which would make the ordering meaningless.
bool order_by_centre(const Cube6& faces, Cube6& out) noexcept {
    std::array<bool, 6> seen{};
    for (const auto& face : faces) {
        std::size_t position = 6;
        for (std::size_t p = 0; p < 6; ++p) {
            if (cube::solver::canonical_color(static_cast<cube::solver::Face>(p)) == face[4]) {
                position = p;
            }
        }
        if (position == 6 || seen[position]) {
            return false;
        }
        seen[position] = true;
        out[position] = face;
    }
    return true;
}

struct Breakdown {
    int valid = 0;
    int solvable = 0;
    bool centres_distinct = true;
};

// How many of the 4096 roll combinations clear each constraint. valid and
// built are deliberately one column, not two: from_facelets succeeds
// exactly when is_physically_valid does.
Breakdown breakdown(const Cube6& faces) noexcept {
    Cube6 base{};
    Breakdown out;
    if (!order_by_centre(faces, base)) {
        out.centres_distinct = false;
        return out;
    }
    std::array<std::array<Face9, 4>, 6> rotations{};
    for (std::size_t f = 0; f < 6; ++f) {
        for (int turns = 0; turns < 4; ++turns) {
            rotations[f][static_cast<std::size_t>(turns)] = rotate(base[f], turns);
        }
    }
    for (unsigned combo = 0; combo < 4096; ++combo) {
        Cube6 candidate{};
        unsigned digits = combo;
        for (std::size_t f = 0; f < 6; ++f) {
            candidate[f] = rotations[f][digits & 3U];
            digits >>= 2U;
        }
        const auto cube = cube::solver::Cube::from_facelets(candidate);
        if (!cube) {
            continue;
        }
        ++out.valid;
        if (cube->is_solvable()) {
            ++out.solvable;
        }
    }
    return out;
}

std::array<std::array<cube::model::Facelet, 9>, 6> to_facelets(const Cube6& faces) noexcept {
    std::array<std::array<cube::model::Facelet, 9>, 6> out{};
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t k = 0; k < 9; ++k) {
            out[f][k].color = facet_of(faces[f][k]);
        }
    }
    return out;
}

// Smallest number of disagreeing cells between two readings of the same
// centre colour, minimised over the four rolls - each clip's consensus
// carries its own arbitrary roll, so a raw cell-for-cell diff would score
// alignment rather than content.
int face_disagreement(const Face9& lhs, const Face9& rhs) noexcept {
    int best = 9;
    for (int turns = 0; turns < 4; ++turns) {
        const Face9 rolled = rotate(rhs, turns);
        int differing = 0;
        for (std::size_t k = 0; k < 9; ++k) {
            if (rolled[k] != lhs[k]) {
                ++differing;
            }
        }
        best = std::min(best, differing);
    }
    return best;
}

// Total disagreement between two clips, pairing faces by centre colour.
int clip_disagreement(const Cube6& lhs, const Cube6& rhs) noexcept {
    int total = 0;
    for (const auto& left : lhs) {
        for (const auto& right : rhs) {
            if (left[4] == right[4]) {
                total += face_disagreement(left, right);
            }
        }
    }
    return total;
}

// A cube whose census and centres are real but whose other 48 stickers are
// arbitrary. The chance floor for the agreement statistic, and the null
// model for how discriminating a validity pass is.
Cube6 random_census_cube(std::mt19937& rng) {
    std::vector<Color> pool;
    for (std::size_t p = 0; p < 6; ++p) {
        const Color color = cube::solver::canonical_color(static_cast<cube::solver::Face>(p));
        for (int i = 0; i < 8; ++i) {
            pool.push_back(color);
        }
    }
    std::shuffle(pool.begin(), pool.end(), rng);
    Cube6 out{};
    std::size_t next = 0;
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t k = 0; k < 9; ++k) {
            if (k == 4) {
                out[f][k] = cube::solver::canonical_color(static_cast<cube::solver::Face>(f));
            } else {
                out[f][k] = pool[next++];
            }
        }
    }
    return out;
}

Cube6 faces_of_cube(const cube::solver::Cube& cube) noexcept {
    Cube6 out{};
    for (std::size_t f = 0; f < 6; ++f) {
        out[f] = cube.face(static_cast<cube::solver::Face>(f));
    }
    return out;
}

// Corrupt a genuine scan by swapping pairs of non-centre stickers, which
// perturbs the cube while preserving the 9-of-each census - the same shape
// of error the consensus makes, since resolve_colors enforces the census.
Cube6 corrupt(const Cube6& faces, int swaps, std::mt19937& rng) {
    Cube6 out = faces;
    std::uniform_int_distribution<int> pick(0, 53);
    for (int s = 0; s < swaps; ++s) {
        std::size_t a = 0;
        std::size_t b = 0;
        do {
            a = static_cast<std::size_t>(pick(rng));
        } while (a % 9 == 4);
        do {
            b = static_cast<std::size_t>(pick(rng));
        } while (b % 9 == 4 || b == a);
        std::swap(out[a / 9][a % 9], out[b / 9][b % 9]);
    }
    return out;
}

int stickers_differing(const Cube6& lhs, const Cube6& rhs) noexcept {
    int total = 0;
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t k = 0; k < 9; ++k) {
            if (lhs[f][k] != rhs[f][k]) {
                ++total;
            }
        }
    }
    return total;
}

}  // namespace

TEST_CASE("CentreClusterVerify", "[.cc-verify]") {
    std::mt19937 rng(20260720U);

    // 1. The reported verdict, recomputed from the pinned consensus faces
    // through the production assembler rather than through the prototype's
    // own pipeline.
    std::printf("\n=== assembly verdict per clip ===\n");
    for (const auto& clip : kConsensus) {
        const Cube6 faces = faces_of(clip);
        const Breakdown plain = breakdown(faces);
        Cube6 mirrored{};
        for (std::size_t f = 0; f < 6; ++f) {
            mirrored[f] = mirror(faces[f]);
        }
        const Breakdown flipped = breakdown(mirrored);
        const auto assembled =
            cube::solver::assemble_scanned_faces_with_mapping(to_facelets(faces));
        std::printf(
            "  %s centres_distinct=%d valid=%4d solvable=%4d | MIRRORED valid=%4d solvable=%4d | "
            "assemble=%s\n",
            clip.clip, plain.centres_distinct ? 1 : 0, plain.valid, plain.solvable, flipped.valid,
            flipped.solvable, assembled.has_value() ? "OK" : "failed");
        if (assembled.has_value()) {
            const auto solution = cube::solver::solve(assembled->cube);
            std::printf("    SOLVER: %s\n", solution.has_value() ? "solved" : "unsolvable");
        }
    }

    // 2. Cross-clip agreement WITH its controls. live1..live4 share a
    // scramble and should agree; live5 does not and should not; a
    // census-preserving shuffle is the floor any of it has to beat.
    std::printf("\n=== cross-clip disagreement (cells of 54; lower = more agreement) ===\n");
    for (std::size_t a = 0; a < kConsensus.size(); ++a) {
        for (std::size_t b = a + 1; b < kConsensus.size(); ++b) {
            const bool same_scramble = a < 4 && b < 4;
            std::printf("  %s vs %s: %2d/54   %s\n", kConsensus[a].clip, kConsensus[b].clip,
                        clip_disagreement(faces_of(kConsensus[a]), faces_of(kConsensus[b])),
                        same_scramble ? "(same scramble)" : "(DIFFERENT scramble - control)");
        }
    }
    {
        int total = 0;
        constexpr int kTrials = 200;
        for (int trial = 0; trial < kTrials; ++trial) {
            total += clip_disagreement(faces_of(kConsensus[0]), random_census_cube(rng));
        }
        std::printf("  chance floor (shuffled stickers, %d trials): %.1f/54\n", kTrials,
                    static_cast<double>(total) / kTrials);
    }

    // 3. What a validity pass is worth. If arbitrary sticker sets clear the
    // check often, then "physical validity selects the arrangement" cannot
    // select anything, and a clip that DID assemble would prove nothing.
    std::printf("\n=== discriminating power of the validity/solvability filter ===\n");
    {
        constexpr int kTrials = 2000;
        int any_valid = 0;
        int any_solvable = 0;
        long long valid_combos = 0;
        for (int trial = 0; trial < kTrials; ++trial) {
            const Breakdown result = breakdown(random_census_cube(rng));
            valid_combos += result.valid;
            if (result.valid > 0) {
                ++any_valid;
            }
            if (result.solvable > 0) {
                ++any_solvable;
            }
        }
        std::printf(
            "  random census cubes (%d trials): %.2f%% have >=1 physically-valid rotation, "
            "%.2f%% have >=1 SOLVABLE rotation\n",
            kTrials, 100.0 * any_valid / kTrials, 100.0 * any_solvable / kTrials);
        std::printf("  mean physically-valid rotations per random cube: %.3f of 4096\n",
                    static_cast<double>(valid_combos) / kTrials);
    }
    {
        // The same question asked of genuine scans: a correct scan should
        // survive at a UNIQUE cube, not at many, or the assembler's own
        // ambiguity rule would reject it.
        int unique = 0;
        constexpr int kSeeds = 50;
        for (unsigned seed = 1; seed <= kSeeds; ++seed) {
            const Cube6 faces = faces_of_cube(cube::solver::random_scramble(seed));
            if (cube::solver::assemble_scanned_faces_with_mapping(to_facelets(faces)).has_value()) {
                ++unique;
            }
        }
        std::printf("  genuine scrambles assembling unambiguously: %d of %d\n", unique, kSeeds);
    }

    // 4. The error budget. How many wrong stickers can a scan carry and
    // still assemble? Read against the cross-clip disagreement above, this
    // says whether the consensus is near-miss or nowhere near.
    std::printf("\n=== error budget: assembly vs number of corrupted stickers ===\n");
    for (int swaps = 0; swaps <= 8; ++swaps) {
        constexpr int kTrials = 400;
        int assembled = 0;
        int valid_any = 0;
        long long changed = 0;
        for (int trial = 0; trial < kTrials; ++trial) {
            const Cube6 truth = faces_of_cube(
                cube::solver::random_scramble(static_cast<unsigned>(trial % 50) + 1U));
            const Cube6 damaged = corrupt(truth, swaps, rng);
            changed += stickers_differing(truth, damaged);
            if (breakdown(damaged).valid > 0) {
                ++valid_any;
            }
            if (cube::solver::assemble_scanned_faces_with_mapping(to_facelets(damaged))
                    .has_value()) {
                ++assembled;
            }
        }
        std::printf("  %d swaps (~%.1f stickers wrong): %.1f%% physically valid, %.1f%% assemble\n",
                    swaps, static_cast<double>(changed) / kTrials, 100.0 * valid_any / kTrials,
                    100.0 * assembled / kTrials);
    }

    SUCCEED();
}
