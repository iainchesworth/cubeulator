#include <cube/vision/track_fusion.hpp>

#include <cube/profiling.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace cube::vision::fusion {

namespace {

// The 24 rotations' face permutations, built once by closure over two
// quarter-turn generators (same math as the loop's stitch helpers).
// perm[face_new] = face_old under the rotation.
const std::vector<std::pair<cv::Matx33d, std::array<int, 6>>>& face_permutations() {
    static const std::vector<std::pair<cv::Matx33d, std::array<int, 6>>> perms = [] {
        const cv::Matx33d rot_y(0, 0, 1, 0, 1, 0, -1, 0, 0);
        const cv::Matx33d rot_z(0, -1, 0, 1, 0, 0, 0, 0, 1);
        std::vector<cv::Matx33d> rotations{cv::Matx33d::eye()};
        for (std::size_t i = 0; i < rotations.size(); ++i) {
            for (const auto& generator : {rot_y, rot_z}) {
                const cv::Matx33d candidate = generator * rotations[i];
                const bool known = std::any_of(
                    rotations.begin(), rotations.end(),
                    [&](const cv::Matx33d& r) { return cv::norm(r - candidate) < 0.1; });
                if (!known) {
                    rotations.push_back(candidate);
                }
            }
        }
        std::vector<std::pair<cv::Matx33d, std::array<int, 6>>> out;
        for (const auto& rotation : rotations) {
            std::array<int, 6> perm{};
            for (int face = 0; face < 6; ++face) {
                cv::Vec3d axis(0, 0, 0);
                axis[face / 2] = (face % 2 == 0) ? 1.0 : -1.0;
                const cv::Vec3d mapped = rotation * axis;
                for (int target = 0; target < 3; ++target) {
                    if (std::abs(mapped[target]) > 0.9) {
                        perm[static_cast<std::size_t>(face)] =
                            target * 2 + (mapped[target] > 0 ? 0 : 1);
                    }
                }
            }
            out.emplace_back(rotation, perm);
        }
        return out;
    }();
    return perms;
}

// A face's fused content at the registration scoring floor - nullopt when
// too thin to carry ANY registration role (scoring or verify).
[[nodiscard]] std::optional<CapturedFace> fused_face(const FaceAggregator& aggregator) {
    if (aggregator.observation_count() < static_cast<std::size_t>(kRegisterScoreFloor)) {
        return std::nullopt;
    }
    return aggregator.capture();
}

// P6.11: the cell cost is the P6.7 decomposed metric, not plain Lab. Two
// fused reads of the SAME sticker taken in different gauge eras differ by
// illumination, and illumination acts as a near-scalar on the whole Lab
// vector (the resolve-side measurement, see assignment_distance in
// color_resolution.cpp): sweep_gentle's era-3 bridge face was read
// grazing-oblique in one era and face-on in the other under the fixed
// light, and the same nine stickers scored 24.8 plain Lab - outside any
// honest plain-Lab radius. What shading does not move is the hue
// direction, so lightness and chroma share the down-weight and the hue
// residual keeps full weight. The weight is 0.2 by the resolve-side
// derivation (mid-plateau between the shading cliff at 1.0 and the
// white-collapse cliff at 0.0); reimplemented locally rather than
// exported because color_resolution's constants were measured against ITS
// callers, and a shared symbol would couple two independently-derived
// bars.
[[nodiscard]] double cell_cost(const cv::Vec3f& a, const cv::Vec3f& b) {
    constexpr double kShadingAxisWeight = 0.2;
    const double dl = static_cast<double>(a[0] - b[0]);
    const double da = static_cast<double>(a[1] - b[1]);
    const double db = static_cast<double>(a[2] - b[2]);
    const double dc = static_cast<double>(std::hypot(a[1], a[2]) - std::hypot(b[1], b[2]));
    // |dc| <= sqrt(da^2+db^2) by the reverse triangle inequality; the
    // clamp guards rounding.
    const double dh_squared = std::max(0.0, da * da + db * db - dc * dc);
    return std::sqrt(kShadingAxisWeight * (dl * dl + dc * dc) + dh_squared);
}

// Cell index mapping under a candidate rotation (newer-frame ->
// older-frame): a cell's 3-D centre in the newer model frame maps through
// the rotation and decomposes on the older face's local (u, v) axes - the
// roll falls out of the geometry, no dihedral search. Outside-view (u, v)
// per face_projection.cpp: the cyclic pair on negative faces, swapped on
// positive ones.
[[nodiscard]] std::pair<int, int> map_cell(const cv::Matx33d& rotation, int face_new, int cell) {
    const int axis_n = face_new / 2;
    const double sign_n = (face_new % 2 == 0) ? 1.0 : -1.0;
    const int u_n = (face_new % 2 == 0) ? (axis_n + 2) % 3 : (axis_n + 1) % 3;
    const int v_n = (face_new % 2 == 0) ? (axis_n + 1) % 3 : (axis_n + 2) % 3;
    cv::Vec3d p(0, 0, 0);
    p[axis_n] = sign_n;
    p[u_n] = (cell % 3 - 1) * (2.0 / 3.0);
    p[v_n] = (cell / 3 - 1) * (2.0 / 3.0);
    const cv::Vec3d q = rotation * p;
    int face_old = -1;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(q[axis]) > 0.9) {
            face_old = axis * 2 + (q[axis] > 0 ? 0 : 1);
        }
    }
    if (face_old < 0) {
        return {-1, -1};
    }
    const int axis_o = face_old / 2;
    const int u_o = (face_old % 2 == 0) ? (axis_o + 2) % 3 : (axis_o + 1) % 3;
    const int v_o = (face_old % 2 == 0) ? (axis_o + 1) % 3 : (axis_o + 2) % 3;
    const int col = static_cast<int>(std::lround(q[u_o] * 1.5)) + 1;
    const int row = static_cast<int>(std::lround(q[v_o] * 1.5)) + 1;
    if (col < 0 || col > 2 || row < 0 || row > 2) {
        return {-1, -1};
    }
    return {face_old, row * 3 + col};
}

}  // namespace

FeedResult add_observation(FusionStore& fusion, std::map<int, int>& generation_of_epoch,
                           int gauge_epoch, int face_id, const FaceObservation& observation) {
    CUBE_ZONE_SCOPED_N("vision.track_fusion.add_observation");
    FeedResult result;
    int& generation = generation_of_epoch[gauge_epoch];
    int sub = gauge_epoch * 1000 + generation;
    const auto faces_it = fusion.find(sub);
    if (faces_it != fusion.end()) {
        const auto slot = faces_it->second.find(face_id);
        if (slot != faces_it->second.end() && slot->second.observation_count() >= 2 &&
            cv::norm(observation.lab[4] - slot->second.capture().lab[4]) > kTakeoverJumpLab) {
            ++generation;
            result.generation_split = true;
            sub = gauge_epoch * 1000 + generation;
        }
    }
    fusion[sub][face_id].add(observation);
    result.sub_epoch = sub;
    return result;
}

RegistrationStats register_epochs(FusionStore& fusion) {
    CUBE_ZONE_SCOPED_N("vision.track_fusion.register_epochs");
    RegistrationStats stats;
    // The radius and margin judge the BEST-SUPPORTED shared face - the
    // minimum per-face mean cell cost - never a pool over all shared
    // faces. Shared faces are not equally trustworthy, and a pool lets
    // the worst one out-vote the best: measured on the sweep (pre-P6.11,
    // in the plain-Lab cost the scorer then used), a 2-observation face
    // fused from mixed-pixel grazing samples costs 45.7 Lab against its
    // own physical face while a converged shared face costs 9.2, and the
    // pooled 27.4 blew the radius, refusing a true merge that commits at
    // 9.2. One clean face's nine cells are the discriminating evidence;
    // a corrupt companion face still gets its veto through the per-face
    // verify, not through the score.
    bool merged_any = true;
    while (merged_any && fusion.size() > 1) {
        merged_any = false;
        for (auto older = fusion.begin(); older != fusion.end() && !merged_any; ++older) {
            for (auto newer = std::next(older); newer != fusion.end() && !merged_any; ++newer) {
                // Content decides across ALL 24 candidates: one shared
                // face's NINE cells discriminate by themselves (a wrong
                // relabel scrambles the grid). The geometric shortlist
                // became unnecessary once scoring went cell-level - and a
                // >65-degree drift put the true relabel outside it anyway
                // (measured pre-P6.11 in plain Lab: both shortlist
                // candidates cost >114 while the 1<-2 merge chose 6.9 vs
                // 108.5).
                double best_cost = -1.0;
                double second_cost = -1.0;
                bool best_verified = false;
                const std::array<int, 6>* best_perm = nullptr;
                const cv::Matx33d* best_rotation = nullptr;
                // P6.13 retry pass: the same selection restricted to
                // candidates the per-face verify does not refute (below).
                double best_v_cost = -1.0;
                double second_v_cost = -1.0;
                const std::array<int, 6>* best_v_perm = nullptr;
                const cv::Matx33d* best_v_rotation = nullptr;
                for (const auto& [rotation, perm] : face_permutations()) {
                    double cost = -1.0;  // min per-face mean cell cost
                    int shared = 0;
                    // Per-candidate content refutation - the SAME
                    // per-face centre verify the commit applies, asked of
                    // every candidate during scoring (P6.13, see the
                    // decision below).
                    bool candidate_verified = true;
                    for (const auto& [face, aggregator] : newer->second) {
                        const auto newer_face = fused_face(aggregator);
                        if (!newer_face) {
                            continue;
                        }
                        const auto older_it =
                            older->second.find(perm[static_cast<std::size_t>(face)]);
                        if (older_it == older->second.end()) {
                            continue;
                        }
                        const auto older_face = fused_face(older_it->second);
                        if (!older_face) {
                            continue;
                        }
                        ++shared;
                        if (cv::norm(newer_face->lab[4] - older_face->lab[4]) >
                            kRegisterVerifyCentreLab) {
                            candidate_verified = false;
                        }
                        double face_total = 0.0;
                        int face_cells = 0;
                        for (int cell = 0; cell < 9; ++cell) {
                            const auto [face_old, cell_old] = map_cell(rotation, face, cell);
                            if (face_old != perm[static_cast<std::size_t>(face)] || cell_old < 0) {
                                continue;
                            }
                            face_total +=
                                cell_cost(newer_face->lab[static_cast<std::size_t>(cell)],
                                          older_face->lab[static_cast<std::size_t>(cell_old)]);
                            ++face_cells;
                        }
                        // Only a full nine-cell grid may carry the score:
                        // a partially mapped face is a geometry
                        // inconsistency, not evidence.
                        if (face_cells < 9) {
                            continue;
                        }
                        const double face_cost = face_total / face_cells;
                        if (cost < 0.0 || face_cost < cost) {
                            cost = face_cost;
                        }
                    }
                    if (shared < 1 || cost < 0.0) {
                        continue;
                    }
                    if (best_cost < 0.0 || cost < best_cost) {
                        second_cost = best_cost;
                        best_cost = cost;
                        best_verified = candidate_verified;
                        best_perm = &perm;
                        best_rotation = &rotation;
                    } else if (second_cost < 0.0 || cost < second_cost) {
                        second_cost = cost;
                    }
                    if (candidate_verified) {
                        if (best_v_cost < 0.0 || cost < best_v_cost) {
                            second_v_cost = best_v_cost;
                            best_v_cost = cost;
                            best_v_perm = &perm;
                            best_v_rotation = &rotation;
                        } else if (second_v_cost < 0.0 || cost < second_v_cost) {
                            second_v_cost = cost;
                        }
                    }
                }
                // The bars: commit radius, margin over the runner-up, and
                // the per-face verify - exactly as before on the primary
                // pass (identical winners, identical refusal accounting).
                const std::array<int, 6>* chosen_perm = nullptr;
                const cv::Matx33d* chosen_rotation = nullptr;
                if (best_perm != nullptr && best_cost <= kRegisterRadiusHue &&
                    (second_cost < 0.0 || second_cost - best_cost >= kRegisterMarginHue)) {
                    if (best_verified) {
                        chosen_perm = best_perm;
                        chosen_rotation = best_rotation;
                    } else {
                        ++stats.refusals;  // shared centres disagree: refuse
                    }
                }
                // P6.13 retry: when the primary pass refuses, re-select
                // among candidates the per-face verify would not refute.
                // A candidate whose shared centres disagree beyond
                // kRegisterVerifyCentreLab can never commit - the winner
                // check above has always enforced that - so it should not
                // crowd the margin either. Measured on the live-demo
                // session: an island holding a converged blue face could
                // not join the main epoch because three of the four rolls
                // that matched its near-uniform shared yellow face within
                // 0.1 cost mapped blue onto the settled green/orange/red
                // slots (centre disagreement 56-80 Lab, each refutable),
                // leaving margin 0.03 against candidates that could never
                // commit; the one surviving roll is the cube's own
                // structure forcing the move, and it commits at margin
                // 30.0. On the seven-clip synthetic corpus this retry
                // changes NOTHING: every clip's merges, refusals and
                // final store are identical (occlusion's 49 refusals all
                // stand, and all its ambiguous thin pairs stay refused -
                // their competing rolls land on slots without data, which
                // no verify can refute).
                if (chosen_perm == nullptr && best_v_perm != nullptr &&
                    best_v_cost <= kRegisterRadiusHue &&
                    (second_v_cost < 0.0 || second_v_cost - best_v_cost >= kRegisterMarginHue)) {
                    chosen_perm = best_v_perm;
                    chosen_rotation = best_v_rotation;
                }
                if (chosen_perm == nullptr) {
                    continue;
                }
                // Merge: transplant relabelled faces the older epoch
                // lacks; NEVER overwrite settled ones it has.
                // Never-overwrite protects settled evidence, not thin
                // evidence. An incumbent still below the capture bar
                // (FaceAggregator::kMinObservations) yields to a
                // transplant that has already converged() - the candidate
                // cleared radius, margin and verify to get here, the
                // incumbent cleared nothing but arrival order: measured
                // on the sweep, a 2-obs unconverged incumbent (the same
                // mixed-pixel grazing reads scored above) otherwise kept
                // its slot against an 8-obs converged read of the same
                // face, and the epoch finished a face short. A sub-floor
                // incumbent (fewer than kRegisterScoreFloor observations
                // - too thin to have participated in scoring or verify,
                // an unverified stray read) yields to any floor-clearing
                // transplant: a 1-obs stray otherwise blocks an 8-obs
                // converged face and the merge transplants nothing.
                // P6.6: re-index each transplanted face's cells through
                // the SAME map_cell used for scoring, so it sheds its
                // source-frame grid roll (otherwise absorbed by
                // assembly's per-face search but defeating rotation-hint
                // digits and cross-generation cell comparisons). If the
                // mapping disagrees with the face permutation or is not a
                // valid permutation, transplant as-is rather than corrupt
                // content.
                // P6.15: STRONGEST EVIDENCE FIRST. The permutation is a
                // bijection, so each target takes at most one candidate and
                // this order changes nothing on its own - it exists so the
                // duplicate refusal below arbitrates by evidence instead of
                // by label number. When a relabelled epoch offers one
                // physical colour under two labels (live-demo-p6-retry's
                // sub-epoch 1002: the red face under labels 1 and 2, 8
                // observations against 5), the copy that survives should be
                // the better-supported one, not whichever label sorts
                // lower.
                std::vector<std::map<int, FaceAggregator>::iterator> transplant_order;
                transplant_order.reserve(newer->second.size());
                for (auto it = newer->second.begin(); it != newer->second.end(); ++it) {
                    transplant_order.push_back(it);
                }
                std::stable_sort(transplant_order.begin(), transplant_order.end(),
                                 [](const auto& a, const auto& b) {
                                     if (a->second.converged() != b->second.converged()) {
                                         return a->second.converged();
                                     }
                                     return a->second.observation_count() >
                                            b->second.observation_count();
                                 });
                for (const auto& entry : transplant_order) {
                    const int face = entry->first;
                    auto& aggregator = entry->second;
                    const int target = (*chosen_perm)[static_cast<std::size_t>(face)];
                    const auto incumbent = older->second.find(target);
                    const bool incumbent_yields =
                        incumbent != older->second.end() &&
                        ((incumbent->second.observation_count() <
                              static_cast<std::size_t>(kRegisterScoreFloor) &&
                          aggregator.observation_count() >=
                              static_cast<std::size_t>(kRegisterScoreFloor)) ||
                         (incumbent->second.observation_count() <
                              FaceAggregator::kMinObservations &&
                          aggregator.converged()));
                    // P6.15: never plant a second copy of a colour the
                    // target epoch already holds settled. The centre is
                    // roll-invariant, so this reads the same before and
                    // after the cell re-index below.
                    bool duplicates_settled = false;
                    if (aggregator.observation_count() >=
                        static_cast<std::size_t>(kRegisterScoreFloor)) {
                        const cv::Vec3f centre = aggregator.capture().lab[4];
                        for (const auto& [other_id, other] : older->second) {
                            if (other_id == target ||
                                other.observation_count() <
                                    static_cast<std::size_t>(kRegisterScoreFloor)) {
                                continue;
                            }
                            if (cv::norm(centre - other.capture().lab[4]) <
                                kCrossSlotDuplicateLab) {
                                duplicates_settled = true;
                                break;
                            }
                        }
                    }
                    if (duplicates_settled) {
                        continue;
                    }
                    if (incumbent == older->second.end() || incumbent_yields) {
                        std::array<int, 9> new_index_of_cell{};
                        std::array<bool, 9> cell_taken{};
                        bool mapping_ok = true;
                        for (int cell = 0; cell < 9 && mapping_ok; ++cell) {
                            const auto [face_old, cell_old] =
                                map_cell(*chosen_rotation, face, cell);
                            if (face_old != target || cell_old < 0 || cell_old > 8 ||
                                cell_taken[static_cast<std::size_t>(cell_old)]) {
                                mapping_ok = false;
                                break;
                            }
                            cell_taken[static_cast<std::size_t>(cell_old)] = true;
                            new_index_of_cell[static_cast<std::size_t>(cell)] = cell_old;
                        }
                        if (mapping_ok) {
                            aggregator.relabel_cells(new_index_of_cell);
                        }
                        if (incumbent != older->second.end()) {
                            older->second.erase(incumbent);
                        }
                        older->second.emplace(target, std::move(aggregator));
                    }
                }
                fusion.erase(newer);
                ++stats.merges;
                merged_any = true;
                // Leave the scan now: the `for` increment would otherwise
                // run `++newer` on the iterator just erased (undefined
                // behaviour - the loop condition is only tested after it).
                break;
            }
        }
    }
    return stats;
}

int distinct_converged_faces(const std::map<int, FaceAggregator>& faces) {
    std::vector<cv::Vec3f> counted;
    for (const auto& [face_id, aggregator] : faces) {
        if (!aggregator.converged()) {
            continue;
        }
        const cv::Vec3f centre = aggregator.capture().lab[4];
        const bool duplicate =
            std::any_of(counted.begin(), counted.end(), [&](const cv::Vec3f& seen) {
                return cv::norm(centre - seen) < kCrossSlotDuplicateLab;
            });
        if (!duplicate) {
            counted.push_back(centre);
        }
    }
    return static_cast<int>(counted.size());
}

BestSubEpoch best_sub_epoch(const FusionStore& fusion) {
    BestSubEpoch best;
    for (const auto& [epoch, faces] : fusion) {
        int enough = 0;
        int converged = 0;
        for (const auto& [face_id, aggregator] : faces) {
            enough += aggregator.observation_count() >= FaceAggregator::kMinObservations ? 1 : 0;
            converged += aggregator.converged() ? 1 : 0;
        }
        const int distinct = distinct_converged_faces(faces);
        if (std::tuple(distinct, converged, enough) >
            std::tuple(best.distinct, best.converged, best.observed)) {
            best.sub_epoch = epoch;
            best.observed = enough;
            best.converged = converged;
            best.distinct = distinct;
        }
    }
    return best;
}

}  // namespace cube::vision::fusion
