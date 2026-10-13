// Centre-colour clustering as a REPLACEMENT for face tracking - hidden
// tag, diagnostic only: nothing here changes production behaviour, and
// nothing in production consumes it.
//
// The question it exists to answer. Today a reading reaches a face slot
// only if continuous orientation tracking says which face it is, and the
// [.pattern-id] measurement showed that routing - not capture - is what
// loses faces: every clip carries capture-grade readings of all six faces,
// 9+ times each, and three of six are discarded downstream. That same
// measurement found the CENTRE colour to be the strongest per-face signal
// available (93.1% leave-one-out self-consistency) while the ordered
// sticker pattern managed only 75.3% and keyed on session lighting rather
// than face identity.
//
// So: drop tracking from the identity path entirely. Pool a whole
// session's readings, cluster them by centre colour into six groups, vote
// per cell within each group, and let PHYSICAL VALIDITY pick the whole-cube
// arrangement. A 93% signal is not good enough to identify a face from one
// reading; the wager is that 35-84 readings voting together are.
//
//   set CUBE_CENTRE_CLUSTER_CSV=<readings.csv from "[.pattern-id]">
//   cube_unit_tests.exe "[.centre-cluster]"
//
// The CSV is recorded on demand from live hardware and does not ship with
// the repo, so the harness SKIPS - never fails - when it is not set.
//
// WHAT IS REUSED RATHER THAN REIMPLEMENTED. Every load-bearing step is
// production code, so a pass here is evidence about production and not
// about this file:
//
//   - classify_lab()                     centre colour, hence the clusters
//   - resolve_colors()                   the authoritative 54-sticker
//                                        global colour assignment
//   - assemble_scanned_faces_with_mapping()
//                                        slot assignment by centre colour,
//                                        the 4^6 roll search, and the
//                                        solvability filter that selects
//                                        the arrangement
//   - cube::solver::solve()              the real solver entry point
//
// In particular the whole-cube arrangement problem - six faces onto six
// solver slots with correct adjacency and handedness, 24 orientations, no
// mirrors - is ALREADY solved by assemble_scanned_faces_with_mapping(),
// which fixes slots from centre colours (centres never move) and then
// tries all 4096 roll combinations, keeping only those that build a
// solvable cube. This harness does not search arrangements itself; it only
// has to deliver six believable faces.
//
// ROLL ALIGNMENT. Readings of one face arrive at arbitrary quarter-turn
// rolls, so a per-cell vote taken over unaligned readings is mush. Cells
// here are in SCREEN row-major order (FaceObservation's order, which
// FaceAggregator::capture() preserves), and two screen readings of the
// same physical face differ by a pure in-plane grid rotation - never a
// reflection, both being views of the same face from the same side. The
// four grid rotations ARE the four rolls oriented_cell_lab_distance
// searches over; pattern_id_test.cpp's PatternIdRollsMatchBodyRelabel
// proves that equality against the shipped kBodyRelabel/kFacePermutation
// tables, so aligning over grid rotations here is aligning over production
// rolls. Only RELATIVE alignment within a cluster is needed - the absolute
// roll is exactly what the assembler's own 4^6 search then determines.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace cube::vision;

namespace {

// One capture-grade reading: the nine screen-order Lab cells, the colour
// its centre classified as, and the pass number (time order, for the
// "how long must a scan run" stabilisation curve).
struct Reading {
    std::string clip;
    int pass = 0;
    int centre_class = 0;
    std::array<cv::Vec3f, 9> lab{};
};

// Clockwise 90-degree rotation of a 3x3 grid as a cell permutation:
// out[kRot90[k]] = in[k]. Identical to the generator
// PatternIdRollsMatchBodyRelabel checks the production roll tables against.
constexpr std::array<int, 9> kRot90{2, 5, 8, 1, 4, 7, 0, 3, 6};

std::array<cv::Vec3f, 9> rotate_cells(const std::array<cv::Vec3f, 9>& in, int turns) {
    std::array<cv::Vec3f, 9> out = in;
    for (int turn = 0; turn < turns; ++turn) {
        std::array<cv::Vec3f, 9> next{};
        for (std::size_t k = 0; k < 9; ++k) {
            next[static_cast<std::size_t>(kRot90[k])] = out[k];
        }
        out = next;
    }
    return out;
}

// Mean per-cell Lab distance between two screen-order faces - the same
// quantity oriented_cell_lab_distance scores its roll hypotheses on, over
// the same set of cell permutations.
float cell_distance(const std::array<cv::Vec3f, 9>& a, const std::array<cv::Vec3f, 9>& b) {
    float total = 0.0F;
    for (std::size_t k = 0; k < 9; ++k) {
        total += static_cast<float>(cv::norm(a[k] - b[k]));
    }
    return total / 9.0F;
}

// Best roll of `sample` onto `reference`, with the runner-up distance so
// the caller can report how DECIDED the alignment was. A roll margin near
// zero means the four hypotheses were indistinguishable and the vote that
// follows is being taken over arbitrarily oriented readings.
struct RollFit {
    int turns = 0;
    float best = 0.0F;
    float runner_up = 0.0F;
};

RollFit best_roll(const std::array<cv::Vec3f, 9>& sample,
                  const std::array<cv::Vec3f, 9>& reference) {
    RollFit fit;
    fit.best = std::numeric_limits<float>::max();
    fit.runner_up = std::numeric_limits<float>::max();
    for (int turns = 0; turns < 4; ++turns) {
        const float d = cell_distance(rotate_cells(sample, turns), reference);
        if (d < fit.best) {
            fit.runner_up = fit.best;
            fit.best = d;
            fit.turns = turns;
        } else if (d < fit.runner_up) {
            fit.runner_up = d;
        }
    }
    return fit;
}

// Per-cell consensus over an aligned cluster: the component-wise MEDIAN
// Lab, which shrugs off the occasional blurred or glare-blown reading in a
// way a mean does not.
std::array<cv::Vec3f, 9> consensus(const std::vector<std::array<cv::Vec3f, 9>>& aligned) {
    std::array<cv::Vec3f, 9> out{};
    if (aligned.empty()) {
        return out;  // no readings, no median (indexing values[0] below would be UB)
    }
    for (std::size_t k = 0; k < 9; ++k) {
        for (int component = 0; component < 3; ++component) {
            std::vector<float> values;
            values.reserve(aligned.size());
            for (const auto& face : aligned) {
                values.push_back(face[k][component]);
            }
            std::nth_element(values.begin(),
                             values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2),
                             values.end());
            out[k][component] = values[values.size() / 2];
        }
    }
    return out;
}

// Medoid of a cluster under the roll-invariant distance: the reading whose
// total distance to the rest is smallest. Used as the alignment reference
// so no single arbitrary first reading can drag the whole vote.
std::size_t medoid(const std::vector<Reading>& cluster) {
    std::size_t best_index = 0;
    float best_total = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < cluster.size(); ++i) {
        float total = 0.0F;
        for (std::size_t j = 0; j < cluster.size(); ++j) {
            if (i != j) {
                total += best_roll(cluster[j].lab, cluster[i].lab).best;
            }
        }
        if (total < best_total) {
            best_total = total;
            best_index = i;
        }
    }
    return best_index;
}

// Aligns a cluster to its medoid and returns the rolled faces in the
// cluster's own (time) order, plus the roll-decision statistics.
struct AlignedCluster {
    std::vector<std::array<cv::Vec3f, 9>> faces;
    float mean_roll_margin = 0.0F;
    int undecided = 0;  // readings whose best and runner-up rolls sat within 2 Lab
    // Cluster PURITY. A cluster holding one physical face has readings that
    // all sit close to the medoid once rolled onto it; a cluster that has
    // pooled TWO faces (the failure mode a 93%-accurate centre invites)
    // shows a population sitting far out, and no roll can reconcile those
    // with the rest. `far` counts readings beyond kClosestCentrePairLab -
    // the palette's own minimum inter-centre separation, so a reading past
    // it differs by more than a legitimate colour difference could explain.
    float median_spread = 0.0F;
    int far = 0;
    // Per-reading alignment evidence, parallel to `faces`, so a caller can
    // keep only the readings the alignment was actually sure about.
    std::vector<float> spread;
    std::vector<float> margin;
};

AlignedCluster align(const std::vector<Reading>& cluster) {
    AlignedCluster out;
    const auto& reference = cluster[medoid(cluster)].lab;
    float margin_total = 0.0F;
    std::vector<float> spreads;
    for (const auto& reading : cluster) {
        const RollFit fit = best_roll(reading.lab, reference);
        out.faces.push_back(rotate_cells(reading.lab, fit.turns));
        const float margin = fit.runner_up - fit.best;
        margin_total += margin;
        if (margin < 2.0F) {
            ++out.undecided;
        }
        spreads.push_back(fit.best);
        out.spread.push_back(fit.best);
        out.margin.push_back(margin);
        if (fit.best > kClosestCentrePairLab) {
            ++out.far;
        }
    }
    out.mean_roll_margin = margin_total / static_cast<float>(cluster.size());
    std::nth_element(spreads.begin(),
                     spreads.begin() + static_cast<std::ptrdiff_t>(spreads.size() / 2),
                     spreads.end());
    out.median_spread = spreads[spreads.size() / 2];
    return out;
}

std::vector<Reading> load(const std::string& path) {
    std::vector<Reading> readings;
    std::ifstream in(path);
    if (!in) {
        return readings;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::vector<std::string> field;
        std::stringstream stream(line);
        std::string cell;
        while (std::getline(stream, cell, ',')) {
            field.push_back(cell);
        }
        // Layout is pattern_id_test.cpp's: clip,pass,t_ms,pose_valid,
        // face_good,q*4,corner_view,trk*3,geom_conf,centre_class,cen0..6,
        // then nine L,a,b triples from column 22.
        if (field.size() < 49 || field[4] != "1") {
            continue;
        }
        Reading reading;
        reading.clip = field[0];
        reading.pass = std::stoi(field[1]);
        reading.centre_class = std::stoi(field[14]);
        for (std::size_t k = 0; k < 9; ++k) {
            const std::size_t base = 22 + k * 3;
            reading.lab[k] = cv::Vec3f(std::stof(field[base]), std::stof(field[base + 1]),
                                       std::stof(field[base + 2]));
        }
        readings.push_back(reading);
    }
    return readings;
}

// Per-cell colour-vote margin over an aligned cluster, computed with
// production's own single-sticker classifier: the fraction of readings
// agreeing with the winning colour, and the runner-up's fraction. This is
// the "is the vote comfortable or marginal?" number - a cell at 0.55/0.45
// is a coin toss that happens to have landed.
struct VoteMargin {
    float worst_share = 1.0F;
    int worst_cell = 0;
    float mean_share = 0.0F;
};

VoteMargin vote_margins(const std::vector<std::array<cv::Vec3f, 9>>& aligned) {
    VoteMargin out;
    float share_total = 0.0F;
    for (std::size_t k = 0; k < 9; ++k) {
        std::map<int, int> tally;
        for (const auto& face : aligned) {
            ++tally[static_cast<int>(classify_lab(face[k]).color)];
        }
        int best = 0;
        for (const auto& [color, count] : tally) {
            (void)color;
            best = std::max(best, count);
        }
        const float share = static_cast<float>(best) / static_cast<float>(aligned.size());
        share_total += share;
        if (share < out.worst_share) {
            out.worst_share = share;
            out.worst_cell = static_cast<int>(k);
        }
    }
    out.mean_share = share_total / 9.0F;
    return out;
}

// How far each of the 4096 rotation combinations gets through the cube's
// own constraint chain. See the call site for what each count means.
struct Breakdown {
    int built = 0;     // from_facelets: the stickers form 26 real cubies
    int valid = 0;     // is_physically_valid: those cubies are a permutation of the real pieces
    int solvable = 0;  // is_solvable: corner/edge permutation and orientation parity
};

Breakdown breakdown(const std::array<std::array<cube::model::Facelet, 9>, 6>& facelets) {
    const auto solver_color = [](cube::model::FacetColor color) {
        switch (color) {
            case cube::model::FacetColor::white:
                return cube::solver::Color::White;
            case cube::model::FacetColor::yellow:
                return cube::solver::Color::Yellow;
            case cube::model::FacetColor::green:
                return cube::solver::Color::Green;
            case cube::model::FacetColor::blue:
                return cube::solver::Color::Blue;
            case cube::model::FacetColor::orange:
                return cube::solver::Color::Orange;
            default:
                return cube::solver::Color::Red;
        }
    };
    // Order the six faces the way from_facelets indexes them: by the cube
    // position their centre colour names (the inverse of canonical_color).
    std::array<std::array<cube::solver::Color, 9>, 6> base{};
    for (const auto& face : facelets) {
        std::array<cube::solver::Color, 9> converted{};
        for (std::size_t k = 0; k < 9; ++k) {
            converted[k] = solver_color(face[k].color);
        }
        for (std::size_t position = 0; position < 6; ++position) {
            if (cube::solver::canonical_color(static_cast<cube::solver::Face>(position)) ==
                converted[4]) {
                base[position] = converted;
            }
        }
    }
    std::array<std::array<std::array<cube::solver::Color, 9>, 4>, 6> rotations{};
    for (std::size_t f = 0; f < 6; ++f) {
        rotations[f][0] = base[f];
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            for (std::size_t k = 0; k < 9; ++k) {
                rotations[f][quarter][static_cast<std::size_t>(kRot90[k])] =
                    rotations[f][quarter - 1][k];
            }
        }
    }
    Breakdown out;
    for (unsigned combo = 0; combo < 4096; ++combo) {
        std::array<std::array<cube::solver::Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t f = 0; f < 6; ++f) {
            candidate[f] = rotations[f][digits & 3U];
            digits >>= 2U;
        }
        const auto cube = cube::solver::Cube::from_facelets(candidate);
        if (!cube) {
            continue;
        }
        ++out.built;
        if (!cube->is_physically_valid()) {
            continue;
        }
        ++out.valid;
        if (cube->is_solvable()) {
            ++out.solvable;
        }
    }
    return out;
}

}  // namespace

// Control for the constraint breakdown inside the harness below, which
// reports from_facelets=0 on real scans. That number is only meaningful if
// the same face-ordering and rotation plumbing reports a NON-zero count on
// a cube known to be assemblable - otherwise "no rotation builds a cube"
// would just be describing a bug in the harness. A solved cube is the
// cheapest such witness: every face uniform, so all 4096 rotations must
// build, be physically valid, and be solvable. Runs in the normal suite.
TEST_CASE("CentreClusterBreakdownPlumbing", "[vision][capture-routing]") {
    const auto facet_color = [](cube::solver::Color color) {
        switch (color) {
            case cube::solver::Color::White:
                return cube::model::FacetColor::white;
            case cube::solver::Color::Yellow:
                return cube::model::FacetColor::yellow;
            case cube::solver::Color::Green:
                return cube::model::FacetColor::green;
            case cube::solver::Color::Blue:
                return cube::model::FacetColor::blue;
            case cube::solver::Color::Orange:
                return cube::model::FacetColor::orange;
            default:
                return cube::model::FacetColor::red;
        }
    };
    for (unsigned seed = 1; seed <= 5; ++seed) {
        const auto scrambled = cube::solver::random_scramble(seed);
        // Hand the breakdown the six faces in a DELIBERATELY shuffled order
        // and each at an arbitrary roll - exactly the free-form input the
        // harness feeds it - so the centre-keyed ordering and the rotation
        // search are both genuinely exercised.
        static constexpr std::array<int, 6> kShuffle{3, 5, 0, 4, 1, 2};
        std::array<std::array<cube::model::Facelet, 9>, 6> facelets{};
        for (std::size_t f = 0; f < 6; ++f) {
            const auto raw = scrambled.face(static_cast<cube::solver::Face>(kShuffle[f]));
            std::array<cube::model::Facelet, 9> converted{};
            for (std::size_t k = 0; k < 9; ++k) {
                converted[k].color = facet_color(raw[k]);
            }
            for (int turn = 0; turn < static_cast<int>(f % 4); ++turn) {
                std::array<cube::model::Facelet, 9> next{};
                for (std::size_t k = 0; k < 9; ++k) {
                    next[static_cast<std::size_t>(kRot90[k])] = converted[k];
                }
                converted = next;
            }
            facelets[f] = converted;
        }
        const Breakdown result = breakdown(facelets);
        // A real scramble must survive the whole chain for at least the one
        // rotation combination that undoes the rolls applied above.
        REQUIRE(result.built >= 1);
        REQUIRE(result.valid >= 1);
        REQUIRE(result.solvable >= 1);
    }
}

TEST_CASE("CentreClusterAssembly", "[.centre-cluster]") {
    const char* csv = std::getenv("CUBE_CENTRE_CLUSTER_CSV");
    if (csv == nullptr || *csv == '\0') {
        SKIP("CUBE_CENTRE_CLUSTER_CSV not set - produce it with \"[.pattern-id]\" first");
    }
    const auto readings = load(csv);
    if (readings.empty()) {
        SKIP("no capture-grade readings in " << csv);
    }

    std::map<std::string, std::map<int, std::vector<Reading>>> by_clip;
    for (const auto& reading : readings) {
        by_clip[reading.clip][reading.centre_class].push_back(reading);
    }

    // Consensus faces per clip, keyed by centre class, kept for the
    // cross-clip reproducibility check at the end.
    std::map<std::string, std::map<int, std::array<cube::model::Facelet, 9>>> consensus_by_clip;

    for (const auto& [clip, clusters] : by_clip) {
        std::printf("\n=== %s ===\n", clip.c_str());
        std::printf("clusters: %d\n", static_cast<int>(clusters.size()));

        std::array<std::array<cv::Vec3f, 9>, 6> faces{};
        std::array<std::vector<std::array<cv::Vec3f, 9>>, 6> aligned_by_slot{};
        std::array<int, 6> centre_class_of_slot{};
        std::array<std::array<cv::Vec3f, 9>, 6> gated_faces{};
        std::array<std::array<cv::Vec3f, 9>, 6> medoid_faces{};
        bool gated_ok = true;
        std::size_t slot = 0;
        bool six = clusters.size() == 6;

        for (const auto& [centre_class, cluster] : clusters) {
            const AlignedCluster aligned = align(cluster);
            const VoteMargin margin = vote_margins(aligned.faces);
            std::printf(
                "  centre=%d n=%3d roll_margin=%6.2f undecided=%3d spread=%5.1f far=%3d "
                "vote_mean=%.2f "
                "vote_worst=%.2f(cell %d)\n",
                centre_class, static_cast<int>(cluster.size()),
                static_cast<double>(aligned.mean_roll_margin), aligned.undecided,
                static_cast<double>(aligned.median_spread), aligned.far,
                static_cast<double>(margin.mean_share), static_cast<double>(margin.worst_share),
                margin.worst_cell);

            // Stabilisation: the earliest prefix of readings (time order)
            // whose consensus already equals the all-readings consensus,
            // cell for cell, under production's classifier. This is the
            // "how long must the scan run" number.
            const auto full = consensus(aligned.faces);
            int stable_at = static_cast<int>(aligned.faces.size());
            for (std::size_t n = aligned.faces.size(); n >= 1; --n) {
                const std::vector<std::array<cv::Vec3f, 9>> prefix(
                    aligned.faces.begin(), aligned.faces.begin() + static_cast<std::ptrdiff_t>(n));
                const auto partial = consensus(prefix);
                bool same = true;
                for (std::size_t k = 0; k < 9; ++k) {
                    if (classify_lab(partial[k]).color != classify_lab(full[k]).color) {
                        same = false;
                        break;
                    }
                }
                if (!same) {
                    break;
                }
                stable_at = static_cast<int>(n);
            }
            std::printf("    consensus stable from reading %d of %d\n", stable_at,
                        static_cast<int>(aligned.faces.size()));

            // GATED consensus: keep only readings that sit within the
            // cluster-seed separation of the medoid (so they plausibly show
            // the SAME face) and whose roll beat its runner-up decisively
            // (so they are actually aligned rather than arbitrarily
            // oriented). If the ungated consensus fails only because it
            // pools mismatched readings, this should rescue it.
            std::vector<std::array<cv::Vec3f, 9>> tight;
            for (std::size_t i = 0; i < aligned.faces.size(); ++i) {
                if (aligned.spread[i] <= kMinCentreSeedSeparationLab && aligned.margin[i] >= 8.0F) {
                    tight.push_back(aligned.faces[i]);
                }
            }
            std::printf("    gated: %d of %d readings survive\n", static_cast<int>(tight.size()),
                        static_cast<int>(aligned.faces.size()));

            if (slot < 6) {
                gated_ok = gated_ok && !tight.empty();
                gated_faces[slot] = tight.empty() ? full : consensus(tight);
                medoid_faces[slot] = cluster[medoid(cluster)].lab;
                faces[slot] = full;
                aligned_by_slot[slot] = aligned.faces;
                centre_class_of_slot[slot] = centre_class;
                ++slot;
            }
        }

        if (!six) {
            std::printf("  ROUTING NOT ATTEMPTED: %d clusters, need 6\n",
                        static_cast<int>(clusters.size()));
            continue;
        }

        // Authoritative colour assignment, then the real assembler.
        std::array<CapturedFace, 6> captured{};
        for (std::size_t f = 0; f < 6; ++f) {
            captured[f].lab = faces[f];
        }
        const auto resolved = resolve_colors(captured);
        if (!resolved) {
            std::printf("  resolve_colors FAILED\n");
            continue;
        }
        std::array<std::array<cube::model::Facelet, 9>, 6> facelets{};
        for (std::size_t f = 0; f < 6; ++f) {
            facelets[f] = (*resolved)[f];
        }
        for (std::size_t f = 0; f < 6; ++f) {
            consensus_by_clip[clip][centre_class_of_slot[f]] = facelets[f];
        }
        static constexpr std::array<const char*, 7> kColorName{"?", "W", "Y", "R", "O", "G", "B"};
        for (std::size_t f = 0; f < 6; ++f) {
            std::printf("  face %d:", static_cast<int>(f));
            for (const auto& facelet : facelets[f]) {
                std::printf(" %s", kColorName[static_cast<std::size_t>(facelet.color)]);
            }
            std::printf("\n");
        }
        std::map<int, int> census;
        for (const auto& face : facelets) {
            for (const auto& facelet : face) {
                ++census[static_cast<int>(facelet.color)];
            }
        }
        std::printf("  census:");
        for (const auto& [color, count] : census) {
            std::printf(" %s=%d", kColorName[static_cast<std::size_t>(color)], count);
        }
        std::printf("\n");

        std::printf("  centres:");
        for (const auto& face : facelets) {
            std::printf(" %s", kColorName[static_cast<std::size_t>(face[4].color)]);
        }
        std::printf("\n");

        const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(facelets);
        if (!assembled) {
            // WHICH cells are implicated. assemble_scanned_faces_with_mapping
            // reports every failure as invalid_cube_state, so the diagnosis
            // has to come from outside it: how few sticker corrections would
            // have made this scan assemble? Corrections are SWAPS of two
            // stickers of different colours, never single recolourings,
            // because resolve_colors() has already forced exactly nine of
            // each colour and a lone recolouring would fail on the colour
            // census rather than on the geometry under test.
            //
            // A one- or two-swap repair means the consensus was essentially
            // right and a named handful of cells let it down; needing more
            // than two means the faces themselves are wrong and no amount of
            // cell-level polishing rescues this scan.
            std::printf("  ASSEMBLY FAILED - retrying with the gated consensus\n");
            if (gated_ok) {
                std::array<CapturedFace, 6> gated_captured{};
                for (std::size_t f = 0; f < 6; ++f) {
                    gated_captured[f].lab = gated_faces[f];
                }
                if (const auto gated_resolved = resolve_colors(gated_captured)) {
                    std::array<std::array<cube::model::Facelet, 9>, 6> gated_facelets{};
                    for (std::size_t f = 0; f < 6; ++f) {
                        gated_facelets[f] = (*gated_resolved)[f];
                    }
                    if (const auto gated_assembled =
                            cube::solver::assemble_scanned_faces_with_mapping(gated_facelets)) {
                        std::printf("  GATED ASSEMBLY SUCCEEDED, physically_valid=%d\n",
                                    gated_assembled->cube.is_physically_valid() ? 1 : 0);
                        if (const auto gated_solution =
                                cube::solver::solve(gated_assembled->cube)) {
                            int gated_moves = 0;
                            for (const auto& step : gated_solution->steps) {
                                gated_moves += static_cast<int>(step.moves.size());
                            }
                            std::printf("  GATED SOLVED in %d moves\n", gated_moves);
                        }
                        continue;
                    }
                    std::printf("  gated assembly also failed\n");
                } else {
                    std::printf("  gated resolve_colors failed\n");
                }
            } else {
                std::printf("  gated consensus unavailable (a cluster kept no readings)\n");
            }
            // CONTROL: assemble the six MEDOID readings - one real captured
            // face per cluster, no voting anywhere. This separates the two
            // ways the hypothesis could fail. If the medoids assemble but
            // the consensus does not, the pooling/alignment/voting built
            // here is what broke; if neither assembles, the readings
            // themselves do not describe a consistent cube and no consensus
            // scheme over them was ever going to.
            std::array<CapturedFace, 6> medoid_captured{};
            for (std::size_t f = 0; f < 6; ++f) {
                medoid_captured[f].lab = medoid_faces[f];
            }
            if (const auto medoid_resolved = resolve_colors(medoid_captured)) {
                std::array<std::array<cube::model::Facelet, 9>, 6> medoid_facelets{};
                for (std::size_t f = 0; f < 6; ++f) {
                    medoid_facelets[f] = (*medoid_resolved)[f];
                }
                std::printf("  medoid-only assembly: %s\n",
                            cube::solver::assemble_scanned_faces_with_mapping(medoid_facelets)
                                ? "SUCCEEDED"
                                : "failed");
            } else {
                std::printf("  medoid-only resolve_colors failed\n");
            }

            // WHICH CONSTRAINT. assemble_scanned_faces_with_mapping reports
            // every failure as invalid_cube_state, so the breakdown is taken
            // by walking the same 4096 rotation combinations it walks and
            // recording how far each one gets:
            //
            //   from_facelets   the 54 stickers form 26 legitimately
            //                   labelled cubies at all - i.e. every corner
            //                   and edge carries a colour combination that
            //                   exists on a real cube (adjacency)
            //   physically_valid  cubie labels are a permutation of the real
            //                   cube's pieces
            //   solvable        corner/edge permutation and orientation
            //                   parity
            //
            // Zero from_facelets survivors means the misread stickers make
            // impossible PIECES (e.g. a white/yellow edge) and the scan
            // never reaches a parity question at all - a very different
            // diagnosis from surviving to parity and failing there.
            const Breakdown chain = breakdown(facelets);
            std::printf(
                "  constraint breakdown over 4096 rotations: from_facelets=%d physically_valid=%d "
                "solvable=%d\n",
                chain.built, chain.valid, chain.solvable);

            std::printf("  searching sticker-swap repairs\n");
            struct Cell {
                std::size_t face;
                std::size_t index;
            };
            std::vector<Cell> cells;
            for (std::size_t f = 0; f < 6; ++f) {
                for (std::size_t k = 0; k < 9; ++k) {
                    if (k != 4) {  // centres are fixed points: a centre swap is a different face,
                                   // not a repair
                        cells.push_back(Cell{f, k});
                    }
                }
            }
            const auto swap_cells = [&](std::array<std::array<cube::model::Facelet, 9>, 6>& grid,
                                        const Cell& a, const Cell& b) {
                std::swap(grid[a.face][a.index], grid[b.face][b.index]);
            };
            bool repaired = false;
            for (std::size_t i = 0; i < cells.size() && !repaired; ++i) {
                for (std::size_t j = i + 1; j < cells.size() && !repaired; ++j) {
                    auto trial = facelets;
                    swap_cells(trial, cells[i], cells[j]);
                    if (cube::solver::assemble_scanned_faces_with_mapping(trial)) {
                        std::printf(
                            "    ONE-SWAP repair: face %d cell %d <-> face %d cell %d\n",
                            static_cast<int>(cells[i].face), static_cast<int>(cells[i].index),
                            static_cast<int>(cells[j].face), static_cast<int>(cells[j].index));
                        repaired = true;
                    }
                }
            }
            if (!repaired) {
                std::printf("    no one-swap repair exists (>=2 sticker errors)\n");
            }
            continue;
        }
        std::printf("  ASSEMBLED, physically_valid=%d\n",
                    assembled->cube.is_physically_valid() ? 1 : 0);
        const auto solution = cube::solver::solve(assembled->cube);
        if (!solution) {
            std::printf("  SOLVE FAILED\n");
            continue;
        }
        int moves = 0;
        for (const auto& step : solution->steps) {
            moves += static_cast<int>(step.moves.size());
        }
        std::printf("  SOLVED in %d moves\n", moves);
    }

    // REPRODUCIBILITY, and what it says about the errors.
    //
    // live1..live4 are four recordings of ONE physical scramble, so their
    // consensus faces must agree cell for cell (up to each clip's arbitrary
    // roll) if the consensus is measuring the cube. The comparison below
    // reports, per centre class, the smallest number of disagreeing cells
    // over the four rolls.
    //
    // The distinction this draws is the whole question. If the consensus
    // errors were INDEPENDENT per-reading noise, two independent recordings
    // would disagree wherever either had erred, and more readings would keep
    // driving the error down. If the errors are SYSTEMATIC - a cell that
    // glare or viewing angle biases the same way every time - independent
    // recordings agree with each other and are wrong together, and no
    // quantity of extra readings helps, because the vote entrenches the bias
    // instead of averaging it away. High agreement here alongside the
    // assembly failures above therefore reads as a CEILING on the method,
    // not as a sampling problem.
    std::printf("\n=== cross-clip agreement (live1..live4 share one scramble) ===\n");
    static constexpr std::array<const char*, 7> kColorName{"?", "W", "Y", "R", "O", "G", "B"};
    const std::array<std::string, 4> same_scramble{"live1", "live2", "live3", "live4"};
    for (std::size_t a = 0; a < same_scramble.size(); ++a) {
        for (std::size_t b = a + 1; b < same_scramble.size(); ++b) {
            const auto lhs = consensus_by_clip.find(same_scramble[a]);
            const auto rhs = consensus_by_clip.find(same_scramble[b]);
            if (lhs == consensus_by_clip.end() || rhs == consensus_by_clip.end()) {
                continue;
            }
            int total = 0;
            int cells = 0;
            std::printf("  %s vs %s:", same_scramble[a].c_str(), same_scramble[b].c_str());
            for (const auto& [centre_class, left] : lhs->second) {
                const auto found = rhs->second.find(centre_class);
                if (found == rhs->second.end()) {
                    continue;
                }
                int best = 9;
                for (int turns = 0; turns < 4; ++turns) {
                    // Reuse the same grid rotation the alignment used, on
                    // colours rather than Lab.
                    std::array<cube::model::Facelet, 9> rolled = found->second;
                    for (int turn = 0; turn < turns; ++turn) {
                        std::array<cube::model::Facelet, 9> next{};
                        for (std::size_t k = 0; k < 9; ++k) {
                            next[static_cast<std::size_t>(kRot90[k])] = rolled[k];
                        }
                        rolled = next;
                    }
                    int differing = 0;
                    for (std::size_t k = 0; k < 9; ++k) {
                        if (rolled[k].color != left[k].color) {
                            ++differing;
                        }
                    }
                    best = std::min(best, differing);
                }
                std::printf(" %s:%d", kColorName[static_cast<std::size_t>(left[4].color)], best);
                total += best;
                cells += 9;
            }
            std::printf("  -> %d/%d cells differ\n", total, cells);
        }
    }
    SUCCEED();
}
