// Centre-anchored colour classification - hidden tag, diagnostic only:
// nothing here changes production behaviour and nothing in production
// consumes it.
//
// THE HYPOTHESIS UNDER TEST. Absolute Lab thresholds cannot separate this
// corpus: under warm indoor light the camera boosts the warm channels and
// crushes the cool ones, so on runD the warm stickers arrive at chroma
// 48-74 while green sits at 26-31, blue at 18-24, and the WHITE face at
// 15.7-21.0 - blue and white overlap outright. Per-face mean L spans 49.3
// to 63.6 within one clip, a spread comparable to the gaps between
// colours.
//
// A 3x3 cube has exactly six centres, one per colour, and they never move.
// So the six captured centres are a per-session reference palette measured
// under this session's actual lighting, and a crushed blue centre is
// crushed the same way a crushed blue sticker is. Classify the other 48
// stickers against THAT palette instead of against absolutes.
//
//   set CUBE_CENTRE_PALETTE_CSV=<a "[.cross-session]" tee, or several
//                                separated by ';'>
//   cube_unit_tests.exe "[.centre-palette]"
//
// The corpus is recorded on demand from live hardware and does not ship
// with the repo, so this SKIPS - never fails - when the variable is unset
// or names nothing readable.
//
// WHAT IS REUSED RATHER THAN REIMPLEMENTED, so a result here is evidence
// about production and not about this file:
//   - classify_lab()                        the incumbent classifier, the
//                                           baseline every variant is
//                                           scored against
//   - resolve_colors()                      the authoritative global
//                                           exactly-9-of-each assignment
//   - default_palette()                     the fixed reference palette
//   - assemble_scanned_faces_with_mapping() THE VALIDITY ORACLE: slot
//                                           assignment from centres, the
//                                           4^6 roll search, from_facelets
//                                           and is_solvable. Cube validity
//                                           is never re-decided here.
//
// CENSUS PROVES NOTHING. A constrained assignment can always force nine of
// each colour, so the decisive column below is PHYSICALLY VALID, and the
// only thing that writes it is the assembler.

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/color_classification.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using cube::model::Facelet;
using cube::model::FacetColor;
using cube::vision::CapturedFace;

namespace {

constexpr std::size_t kFaces = 6;
constexpr std::size_t kCells = 9;
constexpr std::size_t kCentre = 4;
constexpr std::size_t kSamples = kFaces * kCells;

constexpr std::array<const char*, 7> kColorName{"?", "W", "Y", "R", "O", "G", "B"};

const char* color_name(FacetColor color) {
    return kColorName[static_cast<std::size_t>(color)];
}

// One clip's end-of-run store: the six slots the production capture layer
// would hand to assembly, indexed by cube::model::Face (U,R,F,D,L,B).
struct Clip {
    std::string name;
    std::array<std::array<cv::Vec3f, kCells>, kFaces> lab{};
    std::array<bool, kFaces> present{};
    [[nodiscard]] bool complete() const {
        return std::all_of(present.begin(), present.end(), [](bool p) { return p; });
    }
};

float lab_distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    const cv::Vec3f diff = a - b;
    return std::sqrt(diff.dot(diff));
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, sep)) {
        out.push_back(field);
    }
    return out;
}

// Parses the "[.cross-session]" tee. Only the end-of-clip `slot,` rows are
// read - those are the bytes production would assemble from. Column layout
// is that harness's schema line: kind,clip,pass,t_ms,epoch,epoch_face,
// store_face,slot,outcome,... ,colors, then 27 Lab floats.
std::optional<Clip> read_clip(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }
    Clip clip;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("slot,", 0) != 0) {
            continue;
        }
        const auto fields = split(line, ',');
        if (fields.size() < 19 + 27) {
            continue;
        }
        clip.name = fields[1];
        const std::string store_face = fields[6];
        const std::string faces = "URFDLB";
        const auto found = faces.find(store_face.empty() ? '?' : store_face[0]);
        if (found == std::string::npos) {
            continue;
        }
        for (std::size_t cell = 0; cell < kCells; ++cell) {
            const std::size_t base = 19 + cell * 3;
            clip.lab[found][cell] = cv::Vec3f(std::stof(fields[base]), std::stof(fields[base + 1]),
                                              std::stof(fields[base + 2]));
        }
        clip.present[found] = true;
    }
    if (clip.name.empty()) {
        return std::nullopt;
    }
    return clip;
}

// ---------------------------------------------------------------------------
// Naming the six centres: a 6-to-6 assignment of mutually distinct points,
// solved by exhausting all 720 permutations against the fixed reference
// palette (6! is trivial, and exhaustive is exact). The runner-up total is
// kept so the caller can say whether the naming was DECIDED or a coin toss.
// ---------------------------------------------------------------------------
struct CentreNaming {
    std::array<FacetColor, kFaces> color{};
    std::array<std::size_t, kFaces> palette_index{};
    float best_total = 0.0F;
    float runner_up_total = 0.0F;
    float min_separation = 0.0F;
    float max_separation = 0.0F;
};

CentreNaming name_centres(const std::array<cv::Vec3f, kFaces>& centres) {
    const cube::vision::ColorPalette palette = cube::vision::default_palette();
    std::array<std::size_t, kFaces> permutation{};
    std::iota(permutation.begin(), permutation.end(), std::size_t{0});
    CentreNaming naming;
    naming.best_total = std::numeric_limits<float>::max();
    naming.runner_up_total = std::numeric_limits<float>::max();
    std::array<std::size_t, kFaces> best = permutation;
    do {
        float total = 0.0F;
        for (std::size_t f = 0; f < kFaces; ++f) {
            total += lab_distance(centres[f], palette.entries[permutation[f]].lab);
        }
        if (total < naming.best_total) {
            naming.runner_up_total = naming.best_total;
            naming.best_total = total;
            best = permutation;
        } else if (total < naming.runner_up_total) {
            naming.runner_up_total = total;
        }
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    naming.palette_index = best;
    for (std::size_t f = 0; f < kFaces; ++f) {
        naming.color[f] = palette.entries[best[f]].color;
    }
    naming.min_separation = std::numeric_limits<float>::max();
    naming.max_separation = 0.0F;
    for (std::size_t a = 0; a < kFaces; ++a) {
        for (std::size_t b = a + 1; b < kFaces; ++b) {
            const float d = lab_distance(centres[a], centres[b]);
            naming.min_separation = std::min(naming.min_separation, d);
            naming.max_separation = std::max(naming.max_separation, d);
        }
    }
    return naming;
}

// ---------------------------------------------------------------------------
// Exactly-9-per-colour assignment of 54 samples to 6 FIXED reference points
// (the centres, or the corrected palette). Deliberately NOT resolve_colors:
// that refines its means Lloyd-style away from the centres, and the whole
// hypothesis here is that the centres themselves are the right anchors.
// Greedy by margin, then pairwise-swap descent, same shape as
// color_resolution.cpp's assign_clusters so the two are comparable.
// ---------------------------------------------------------------------------
std::array<int, kSamples> assign_exactly_nine(const std::array<cv::Vec3f, kSamples>& samples,
                                              const std::array<cv::Vec3f, kFaces>& anchors,
                                              bool pin_centres) {
    constexpr float kInf = std::numeric_limits<float>::infinity();
    std::array<int, kSamples> cluster{};
    cluster.fill(-1);
    std::array<std::size_t, kFaces> load{};
    if (pin_centres) {
        for (std::size_t f = 0; f < kFaces; ++f) {
            cluster[f * kCells + kCentre] = static_cast<int>(f);
            ++load[f];
        }
    }
    for (;;) {
        std::size_t best_sample = kSamples;
        int best_cluster = -1;
        float best_margin = -1.0F;
        for (std::size_t s = 0; s < kSamples; ++s) {
            if (cluster[s] >= 0) {
                continue;
            }
            float nearest = kInf;
            float second = kInf;
            int nearest_cluster = -1;
            for (std::size_t c = 0; c < kFaces; ++c) {
                if (load[c] >= kCells) {
                    continue;
                }
                const float distance = lab_distance(samples[s], anchors[c]);
                if (distance < nearest) {
                    second = nearest;
                    nearest = distance;
                    nearest_cluster = static_cast<int>(c);
                } else if (distance < second) {
                    second = distance;
                }
            }
            const float margin = (second == kInf) ? kInf : second - nearest;
            if (margin > best_margin) {
                best_margin = margin;
                best_sample = s;
                best_cluster = nearest_cluster;
            }
        }
        if (best_sample == kSamples) {
            break;
        }
        cluster[best_sample] = best_cluster;
        ++load[static_cast<std::size_t>(best_cluster)];
    }
    constexpr float kMinImprovement = 1e-3F;
    bool improved = true;
    while (improved) {
        improved = false;
        for (std::size_t a = 0; a < kSamples; ++a) {
            if (pin_centres && a % kCells == kCentre) {
                continue;
            }
            for (std::size_t b = a + 1; b < kSamples; ++b) {
                if ((pin_centres && b % kCells == kCentre) || cluster[a] == cluster[b]) {
                    continue;
                }
                const auto ca = static_cast<std::size_t>(cluster[a]);
                const auto cb = static_cast<std::size_t>(cluster[b]);
                const float current =
                    lab_distance(samples[a], anchors[ca]) + lab_distance(samples[b], anchors[cb]);
                const float swapped =
                    lab_distance(samples[a], anchors[cb]) + lab_distance(samples[b], anchors[ca]);
                if (swapped + kMinImprovement < current) {
                    std::swap(cluster[a], cluster[b]);
                    improved = true;
                }
            }
        }
    }
    return cluster;
}

using Grid = std::array<std::array<Facelet, kCells>, kFaces>;

// ---------------------------------------------------------------------------
// How far each of the 4096 rotation combinations gets through the cube's own
// constraint chain. assemble_scanned_faces_with_mapping reports every failure
// as invalid_cube_state, so the diagnosis has to come from outside it - and
// the distinction matters: built=0 means the stickers make IMPOSSIBLE PIECES
// (a white/yellow edge, say) and the scan never reaches a parity question,
// while built>0 with solvable=0 means the pieces are all real and only the
// parity is wrong. Transcribed from centre_cluster_assembly_test.cpp, whose
// CentreClusterBreakdownPlumbing case is the plumbing control for it.
// ---------------------------------------------------------------------------
constexpr std::array<int, 9> kRot90{2, 5, 8, 1, 4, 7, 0, 3, 6};

struct Breakdown {
    int built = 0;
    int valid = 0;
    int solvable = 0;
};

Breakdown breakdown(const Grid& facelets) {
    const auto solver_color = [](FacetColor color) {
        switch (color) {
            case FacetColor::white:
                return cube::solver::Color::White;
            case FacetColor::yellow:
                return cube::solver::Color::Yellow;
            case FacetColor::green:
                return cube::solver::Color::Green;
            case FacetColor::blue:
                return cube::solver::Color::Blue;
            case FacetColor::orange:
                return cube::solver::Color::Orange;
            default:
                return cube::solver::Color::Red;
        }
    };
    std::array<std::array<cube::solver::Color, 9>, 6> base{};
    for (const auto& face : facelets) {
        std::array<cube::solver::Color, 9> converted{};
        for (std::size_t k = 0; k < kCells; ++k) {
            converted[k] = solver_color(face[k].color);
        }
        for (std::size_t position = 0; position < kFaces; ++position) {
            if (cube::solver::canonical_color(static_cast<cube::solver::Face>(position)) ==
                converted[kCentre]) {
                base[position] = converted;
            }
        }
    }
    std::array<std::array<std::array<cube::solver::Color, 9>, 4>, 6> rotations{};
    for (std::size_t f = 0; f < kFaces; ++f) {
        rotations[f][0] = base[f];
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            for (std::size_t k = 0; k < kCells; ++k) {
                rotations[f][quarter][static_cast<std::size_t>(kRot90[k])] =
                    rotations[f][quarter - 1][k];
            }
        }
    }
    Breakdown out;
    for (unsigned combo = 0; combo < 4096; ++combo) {
        std::array<std::array<cube::solver::Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t f = 0; f < kFaces; ++f) {
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

// ---------------------------------------------------------------------------
// Reporting. Census plus the decisive column, and - when assembly fails -
// how far off it was: how many of the 4096 rotation combinations survived
// each stage, and whether a single two-sticker swap would have repaired it.
// ---------------------------------------------------------------------------
struct Verdict {
    std::map<std::string, int> census;
    int distinct_centres = 0;
    bool assembled = false;
    int swap_repairs = 0;
    std::string first_repair;
};

Verdict judge(const Grid& grid, bool search_repairs) {
    Verdict verdict;
    for (const auto& face : grid) {
        for (const auto& facelet : face) {
            ++verdict.census[color_name(facelet.color)];
        }
    }
    std::vector<FacetColor> centres;
    for (const auto& face : grid) {
        if (std::find(centres.begin(), centres.end(), face[kCentre].color) == centres.end()) {
            centres.push_back(face[kCentre].color);
        }
    }
    verdict.distinct_centres = static_cast<int>(centres.size());
    verdict.assembled = cube::solver::assemble_scanned_faces_with_mapping(grid).has_value();
    if (verdict.assembled || !search_repairs) {
        return verdict;
    }
    // Corrections are SWAPS of two differently-coloured stickers, never
    // lone recolourings: the census is already exactly nine of each here,
    // and a lone recolouring would fail on the census rather than on the
    // geometry under test. Centres are fixed points - swapping one is a
    // different cube, not a repair.
    struct Cell {
        std::size_t face;
        std::size_t index;
    };
    std::vector<Cell> cells;
    for (std::size_t f = 0; f < kFaces; ++f) {
        for (std::size_t k = 0; k < kCells; ++k) {
            if (k != kCentre) {
                cells.push_back(Cell{f, k});
            }
        }
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            if (grid[cells[i].face][cells[i].index].color ==
                grid[cells[j].face][cells[j].index].color) {
                continue;
            }
            Grid trial = grid;
            std::swap(trial[cells[i].face][cells[i].index], trial[cells[j].face][cells[j].index]);
            if (cube::solver::assemble_scanned_faces_with_mapping(trial)) {
                ++verdict.swap_repairs;
                if (verdict.first_repair.empty()) {
                    char text[128];
                    std::snprintf(text, sizeof(text), "%c%zu<->%c%zu", "URFDLB"[cells[i].face],
                                  cells[i].index, "URFDLB"[cells[j].face], cells[j].index);
                    verdict.first_repair = text;
                }
            }
        }
    }
    return verdict;
}

// Bounded TWO-swap search. Unbounded it is 1128^2 assemblies and hours; the
// bound is the only cells a misclassification could plausibly have touched -
// the `budget` stickers whose assignment margin was smallest. If not even
// two swaps among the most-doubtful stickers repair the scan, the errors are
// neither few nor confined to the cells the classifier was unsure about.
int two_swap_repairs(const Grid& grid, const std::array<float, kSamples>& margin,
                     std::size_t budget, std::string& first) {
    std::vector<std::size_t> order;
    for (std::size_t s = 0; s < kSamples; ++s) {
        if (s % kCells != kCentre) {
            order.push_back(s);
        }
    }
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return margin[a] < margin[b]; });
    order.resize(std::min(budget, order.size()));
    std::vector<std::pair<std::size_t, std::size_t>> swaps;
    for (std::size_t i = 0; i < order.size(); ++i) {
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const auto& a = grid[order[i] / kCells][order[i] % kCells];
            const auto& b = grid[order[j] / kCells][order[j] % kCells];
            if (a.color != b.color) {
                swaps.emplace_back(order[i], order[j]);
            }
        }
    }
    const auto apply = [](Grid& g, std::size_t a, std::size_t b) {
        std::swap(g[a / kCells][a % kCells], g[b / kCells][b % kCells]);
    };
    int hits = 0;
    for (std::size_t i = 0; i < swaps.size(); ++i) {
        for (std::size_t j = i + 1; j < swaps.size(); ++j) {
            if (swaps[i].first == swaps[j].first || swaps[i].first == swaps[j].second ||
                swaps[i].second == swaps[j].first || swaps[i].second == swaps[j].second) {
                continue;
            }
            Grid trial = grid;
            apply(trial, swaps[i].first, swaps[i].second);
            apply(trial, swaps[j].first, swaps[j].second);
            if (cube::solver::assemble_scanned_faces_with_mapping(trial)) {
                ++hits;
                if (first.empty()) {
                    char text[160];
                    std::snprintf(text, sizeof(text), "%c%zu<->%c%zu and %c%zu<->%c%zu",
                                  "URFDLB"[swaps[i].first / kCells], swaps[i].first % kCells,
                                  "URFDLB"[swaps[i].second / kCells], swaps[i].second % kCells,
                                  "URFDLB"[swaps[j].first / kCells], swaps[j].first % kCells,
                                  "URFDLB"[swaps[j].second / kCells], swaps[j].second % kCells);
                    first = text;
                }
            }
        }
    }
    return hits;
}

void report(const std::string& label, const Grid& grid, bool search_repairs,
            const std::array<float, kSamples>* margin = nullptr) {
    const Verdict verdict = judge(grid, search_repairs);
    std::printf("  %-38s census", label.c_str());
    for (const auto& [name, count] : verdict.census) {
        std::printf(" %s=%d", name.c_str(), count);
    }
    std::printf(" | centres=%d | VALID=%s", verdict.distinct_centres,
                verdict.assembled ? "YES" : "no");
    if (!verdict.assembled) {
        const Breakdown chain = breakdown(grid);
        std::printf(" | of 4096 rotations built=%d valid=%d solvable=%d", chain.built, chain.valid,
                    chain.solvable);
    }
    if (!verdict.assembled && search_repairs) {
        std::printf(" | one-swap repairs=%d%s%s", verdict.swap_repairs,
                    verdict.first_repair.empty() ? "" : " e.g. ", verdict.first_repair.c_str());
        if (margin != nullptr) {
            std::string first;
            const int two = two_swap_repairs(grid, *margin, 24, first);
            std::printf(" | two-swap repairs among the 24 least-confident cells=%d%s%s", two,
                        first.empty() ? "" : " e.g. ", first.c_str());
        }
    }
    std::printf("\n");
}

// Assignment margin per sample: how much closer the sample sat to its own
// anchor than to the nearest rival, relative to the rival. 1 means it owns
// its cluster outright, 0 means a rival was just as close - which the
// exactly-9 constraint can legitimately force on a borderline sticker. Same
// expression resolve_colors() writes into Facelet::confidence, so the two
// are directly comparable.
std::array<float, kSamples> assignment_margins(const std::array<cv::Vec3f, kSamples>& samples,
                                               const std::array<cv::Vec3f, kFaces>& anchors,
                                               const std::array<int, kSamples>& cluster) {
    std::array<float, kSamples> margin{};
    for (std::size_t s = 0; s < kSamples; ++s) {
        const auto own = static_cast<std::size_t>(cluster[s]);
        const float own_distance = lab_distance(samples[s], anchors[own]);
        float rival = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < kFaces; ++c) {
            if (c != own) {
                rival = std::min(rival, lab_distance(samples[s], anchors[c]));
            }
        }
        margin[s] = rival > 0.0F ? std::clamp((rival - own_distance) / rival, 0.0F, 1.0F) : 0.0F;
    }
    return margin;
}

Grid grid_from_assignment(const std::array<int, kSamples>& cluster,
                          const std::array<FacetColor, kFaces>& name_of) {
    Grid grid{};
    for (std::size_t s = 0; s < kSamples; ++s) {
        grid[s / kCells][s % kCells] =
            Facelet{.color = name_of[static_cast<std::size_t>(cluster[s])], .confidence = 1.0F};
    }
    return grid;
}

}  // namespace

// Positive control for the verdict path below, which reports VALID=no and
// built=0 on every variant of the real corpus. That reading is only
// meaningful if the SAME judge()/breakdown() plumbing says YES on a cube
// known to assemble - otherwise "nothing is valid" would just be describing
// a bug in this file. A solved cube is the cheapest such witness: every face
// uniform, so all 4096 rotations must build, be physically valid and be
// solvable. Runs in the normal suite.
TEST_CASE("CentrePaletteVerdictPlumbing", "[vision][capture-routing]") {
    static constexpr std::array<FacetColor, kFaces> kSolvedCentres{
        FacetColor::yellow, FacetColor::red,    FacetColor::green,
        FacetColor::white,  FacetColor::orange, FacetColor::blue};
    Grid solved{};
    for (std::size_t f = 0; f < kFaces; ++f) {
        solved[f].fill(Facelet{.color = kSolvedCentres[f], .confidence = 1.0F});
    }
    const Verdict verdict = judge(solved, /*search_repairs=*/false);
    CHECK(verdict.distinct_centres == 6);
    CHECK(verdict.assembled);
    for (const auto& [name, count] : verdict.census) {
        CHECK(count == 9);
    }
    const Breakdown chain = breakdown(solved);
    CHECK(chain.built == 4096);
    CHECK(chain.valid == 4096);
    CHECK(chain.solvable == 4096);
}

TEST_CASE("centre-anchored classification against the captured centre palette",
          "[.centre-palette]") {
    const char* csv = std::getenv("CUBE_CENTRE_PALETTE_CSV");
    if (csv == nullptr) {
        SKIP("CUBE_CENTRE_PALETTE_CSV not set - tee a clip through \"[.cross-session]\" first");
    }
    std::vector<Clip> clips;
    for (const auto& path : split(std::string(csv), ';')) {
        if (path.empty()) {
            continue;
        }
        if (auto clip = read_clip(path); clip.has_value() && clip->complete()) {
            clips.push_back(*clip);
        } else {
            std::printf("skipping %s (unreadable or fewer than six slots)\n", path.c_str());
        }
    }
    if (clips.empty()) {
        SKIP("no readable six-face clip in CUBE_CENTRE_PALETTE_CSV");
    }

    const cube::vision::ColorPalette palette = cube::vision::default_palette();

    // Cross-clip pooled reference: the per-colour mean of every clip's
    // centre for that colour. This is the ONLY reference against which a
    // per-face offset is non-degenerate when the palette is built from the
    // centres themselves - see the note where the offset variants run.
    std::map<int, std::vector<cv::Vec3f>> pooled;
    std::vector<CentreNaming> namings;
    for (const auto& clip : clips) {
        std::array<cv::Vec3f, kFaces> centres{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            centres[f] = clip.lab[f][kCentre];
        }
        const CentreNaming naming = name_centres(centres);
        namings.push_back(naming);
        for (std::size_t f = 0; f < kFaces; ++f) {
            pooled[static_cast<int>(naming.color[f])].push_back(centres[f]);
        }
    }
    std::map<int, cv::Vec3f> pooled_mean;
    for (const auto& [color, list] : pooled) {
        cv::Vec3f sum(0.0F, 0.0F, 0.0F);
        for (const auto& lab : list) {
            sum += lab;
        }
        pooled_mean[color] = sum * (1.0F / static_cast<float>(list.size()));
    }

    for (std::size_t c = 0; c < clips.size(); ++c) {
        const Clip& clip = clips[c];
        const CentreNaming& naming = namings[c];
        std::array<cv::Vec3f, kFaces> centres{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            centres[f] = clip.lab[f][kCentre];
        }
        std::array<cv::Vec3f, kSamples> samples{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            for (std::size_t k = 0; k < kCells; ++k) {
                samples[f * kCells + k] = clip.lab[f][k];
            }
        }

        std::printf("\n=== clip %s ===\n", clip.name.c_str());
        std::printf("  centre naming: ");
        for (std::size_t f = 0; f < kFaces; ++f) {
            std::printf("%c=%s(L%.1f,a%.1f,b%.1f,C%.1f) ", "URFDLB"[f], color_name(naming.color[f]),
                        static_cast<double>(centres[f][0]), static_cast<double>(centres[f][1]),
                        static_cast<double>(centres[f][2]),
                        static_cast<double>(std::hypot(centres[f][1], centres[f][2])));
        }
        std::printf(
            "\n  centre separations: min=%.1f max=%.1f (seed floor %.1f, closest legit pair "
            "%.1f)\n",
            static_cast<double>(naming.min_separation), static_cast<double>(naming.max_separation),
            static_cast<double>(cube::vision::kMinCentreSeedSeparationLab),
            static_cast<double>(cube::vision::kClosestCentrePairLab));
        std::printf("  naming margin: best=%.1f runner-up=%.1f (gap %.1f over 720 permutations)\n",
                    static_cast<double>(naming.best_total),
                    static_cast<double>(naming.runner_up_total),
                    static_cast<double>(naming.runner_up_total - naming.best_total));
        // Per-face exposure spread, the drift the offset variants exist to
        // cancel.
        std::printf("  per-face mean L:");
        for (std::size_t f = 0; f < kFaces; ++f) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < kCells; ++k) {
                sum += clip.lab[f][k][0];
            }
            std::printf(" %c=%.1f", "URFDLB"[f], static_cast<double>(sum / 9.0F));
        }
        std::printf("\n");

        // --- V0 CONTROL: the incumbent per-sticker classifier, unpooled.
        Grid incumbent{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            for (std::size_t k = 0; k < kCells; ++k) {
                incumbent[f][k] = cube::vision::classify_lab(clip.lab[f][k]);
            }
        }
        report("V0 classify_lab (incumbent)", incumbent, false);

        // --- V0b CONTROL: production's authoritative resolver.
        std::array<CapturedFace, kFaces> captured{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            captured[f].lab = clip.lab[f];
        }
        Grid resolved_grid{};
        bool have_resolved = false;
        if (const auto resolved = cube::vision::resolve_colors(captured)) {
            for (std::size_t f = 0; f < kFaces; ++f) {
                resolved_grid[f] = (*resolved)[f];
            }
            have_resolved = true;
            std::array<float, kSamples> resolved_margin{};
            for (std::size_t f = 0; f < kFaces; ++f) {
                for (std::size_t k = 0; k < kCells; ++k) {
                    resolved_margin[f * kCells + k] = resolved_grid[f][k].confidence;
                }
            }
            report("V0b resolve_colors (production)", resolved_grid, true, &resolved_margin);
        } else {
            std::printf("  V0b resolve_colors FAILED\n");
        }

        // --- V1: nearest captured centre, no global constraint.
        Grid v1{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            for (std::size_t k = 0; k < kCells; ++k) {
                float best = std::numeric_limits<float>::max();
                std::size_t pick = 0;
                for (std::size_t g = 0; g < kFaces; ++g) {
                    const float d = lab_distance(clip.lab[f][k], centres[g]);
                    if (d < best) {
                        best = d;
                        pick = g;
                    }
                }
                v1[f][k] = Facelet{.color = naming.color[pick], .confidence = 1.0F};
            }
        }
        report("V1 nearest centre (unconstrained)", v1, false);

        // --- V2: nearest captured centre + exactly nine of each.
        const auto v2_cluster = assign_exactly_nine(samples, centres, /*pin_centres=*/true);
        const Grid v2 = grid_from_assignment(v2_cluster, naming.color);
        const auto v2_margin = assignment_margins(samples, centres, v2_cluster);
        report("V2 nearest centre + 9-of-each", v2, true, &v2_margin);

        // --- V3: per-face centre-anchored offset correction against the
        // FIXED palette. Note the degeneracy this variant exists to escape:
        // with the reference palette built from the six centres themselves,
        // a face's offset from "the global reference for its colour" is its
        // own centre minus itself, i.e. exactly zero - so a per-face offset
        // is only non-trivial against an EXTERNAL reference. The two
        // available externals are the fixed palette (here) and the
        // cross-clip pooled centres (V4).
        std::array<cv::Vec3f, kFaces> v3_offset{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            v3_offset[f] = centres[f] - palette.entries[naming.palette_index[f]].lab;
        }
        std::array<cv::Vec3f, kSamples> v3_samples{};
        std::array<cv::Vec3f, kFaces> palette_anchors{};
        for (std::size_t g = 0; g < kFaces; ++g) {
            palette_anchors[g] = palette.entries[naming.palette_index[g]].lab;
        }
        Grid v3_free{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            for (std::size_t k = 0; k < kCells; ++k) {
                const cv::Vec3f corrected = clip.lab[f][k] - v3_offset[f];
                v3_samples[f * kCells + k] = corrected;
                float best = std::numeric_limits<float>::max();
                std::size_t pick = 0;
                for (std::size_t g = 0; g < kFaces; ++g) {
                    const float d = lab_distance(corrected, palette_anchors[g]);
                    if (d < best) {
                        best = d;
                        pick = g;
                    }
                }
                v3_free[f][k] = Facelet{.color = naming.color[pick], .confidence = 1.0F};
            }
        }
        report("V3 per-face offset vs palette (free)", v3_free, false);
        const auto v3_cluster =
            assign_exactly_nine(v3_samples, palette_anchors, /*pin_centres=*/true);
        const Grid v3 = grid_from_assignment(v3_cluster, naming.color);
        const auto v3_margin = assignment_margins(v3_samples, palette_anchors, v3_cluster);
        report("V3 per-face offset vs palette + 9", v3, true, &v3_margin);

        // --- V4: per-face offset against the cross-clip POOLED centre for
        // that colour, then match against the pooled palette. Non-trivial
        // only when more than one clip was loaded.
        std::array<cv::Vec3f, kFaces> pooled_anchors{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            pooled_anchors[f] = pooled_mean[static_cast<int>(naming.color[f])];
        }
        std::array<cv::Vec3f, kSamples> v4_samples{};
        Grid v4_free{};
        for (std::size_t f = 0; f < kFaces; ++f) {
            const cv::Vec3f offset = centres[f] - pooled_anchors[f];
            for (std::size_t k = 0; k < kCells; ++k) {
                const cv::Vec3f corrected = clip.lab[f][k] - offset;
                v4_samples[f * kCells + k] = corrected;
                float best = std::numeric_limits<float>::max();
                std::size_t pick = 0;
                for (std::size_t g = 0; g < kFaces; ++g) {
                    const float d = lab_distance(corrected, pooled_anchors[g]);
                    if (d < best) {
                        best = d;
                        pick = g;
                    }
                }
                v4_free[f][k] = Facelet{.color = naming.color[pick], .confidence = 1.0F};
            }
        }
        report("V4 per-face offset vs pooled (free)", v4_free, false);
        const auto v4_cluster =
            assign_exactly_nine(v4_samples, pooled_anchors, /*pin_centres=*/true);
        const Grid v4 = grid_from_assignment(v4_cluster, naming.color);
        const auto v4_margin = assignment_margins(v4_samples, pooled_anchors, v4_cluster);
        report("V4 per-face offset vs pooled + 9", v4, true, &v4_margin);

        // --- Disagreement map: where the best centre-anchored variant
        // parts company with the incumbent, and whether the disagreements
        // cluster on one face, on peripheral cells, or on the known
        // non-sticker samples (L below 35: shadow, gap or dark shirt).
        const std::array<const Grid*, 4> variants{&v2, &v3, &v4,
                                                  have_resolved ? &resolved_grid : &v2};
        const std::array<const char*, 4> variant_names{"V2", "V3", "V4", "V0b"};
        for (std::size_t v = 0; v < variants.size(); ++v) {
            int differing = 0;
            int dark = 0;
            int peripheral = 0;
            std::array<int, kFaces> per_face{};
            std::string cells;
            for (std::size_t f = 0; f < kFaces; ++f) {
                for (std::size_t k = 0; k < kCells; ++k) {
                    if ((*variants[v])[f][k].color == incumbent[f][k].color) {
                        continue;
                    }
                    ++differing;
                    ++per_face[f];
                    if (clip.lab[f][k][0] < 35.0F) {
                        ++dark;
                    }
                    if (k % 2 == 0) {  // corners of the 3x3
                        ++peripheral;
                    }
                    char text[64];
                    std::snprintf(text, sizeof(text), " %c%zu(%s->%s,L%.0f)", "URFDLB"[f], k,
                                  color_name(incumbent[f][k].color),
                                  color_name((*variants[v])[f][k].color),
                                  static_cast<double>(clip.lab[f][k][0]));
                    cells += text;
                }
            }
            std::printf(
                "  %s vs incumbent: %d/54 differ (dark L<35: %d, corner cells: %d) per-face",
                variant_names[v], differing, dark, peripheral);
            for (std::size_t f = 0; f < kFaces; ++f) {
                std::printf(" %c=%d", "URFDLB"[f], per_face[f]);
            }
            std::printf("\n   %s\n", cells.empty() ? "(none)" : cells.c_str());
        }
    }
    SUCCEED();
}
