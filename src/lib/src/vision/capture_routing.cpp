#include <cube/vision/capture_routing.hpp>

#include <cube/vision/color_resolution.hpp>
#include <cube/vision/orientation_lattice.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cube::vision {

namespace {

using cube::model::Face;

[[nodiscard]] cube::model::FacetColor centre_class(const CapturedFace& face) {
    return classify_lab(face.lab[cube::model::center_facelet_index]).color;
}

// Single-face tight re-association predicate (recovery): all three signals
// must agree - the same live-tested triple the legacy centre-keyed store
// used for "this is a re-show of that face".
[[nodiscard]] bool tight_match(const CapturedFace& a, const CapturedFace& b, const RoutingConfig& config) {
    return centre_lab_distance(a, b) < config.same_face_centre_lab && centre_class(a) == centre_class(b) &&
           census_l1_distance(color_census(a), color_census(b)) <= config.reassoc_census;
}

// "These two readings are of the same PHYSICAL face" - the predicate the
// verified in-place update authenticates on.
//
// Centre class equality is the load-bearing term, not the radius. A cube's
// six centres are six DISTINCT colours, so two readings that agree on
// centre class either are the same face or are a classification failure;
// the radius is then defence in depth against that failure, and is sized
// against kClosestCentrePairLab (see RoutingConfig::same_face_centre_lab
// and the static_assert beside it) so a red centre misread as orange still
// cannot verify onto the orange slot. The class term is what refuses the
// one measured breach in the corpus - live3 slot F yellow -> red at 28.1
// Lab - and the radius is what refuses it a second time.
//
// Deliberately the same predicate the recovery path uses to say "this is a
// re-show of that face" (tight_match), differing only in the census
// allowance: two definitions of "same face" that could disagree is how a
// verifier ends up admitting what registration would have refused.
[[nodiscard]] bool verified_same_face(const CapturedFace& captured, const CapturedFace& stored,
                                      const RoutingConfig& config) {
    return centre_class(captured) == centre_class(stored) &&
           centre_lab_distance(captured, stored) <= config.same_face_centre_lab &&
           census_l1_distance(color_census(captured), color_census(stored)) <= config.verify_census_drift;
}

// Interchangeable-for-assembly: identical sticker population and centres
// inside resolve_colors' own seed-separation floor, i.e. the resolver
// itself could not tell these two slots apart either.
[[nodiscard]] bool content_equivalent(const CapturedFace& a, const CapturedFace& b,
                                      const RoutingConfig& config) {
    return census_l1_distance(color_census(a), color_census(b)) == 0 &&
           centre_lab_distance(a, b) < config.content_equiv_lab;
}

}  // namespace

ColorCensus color_census(const CapturedFace& face) {
    ColorCensus census{};
    for (const auto& lab : face.lab) {
        ++census[static_cast<std::size_t>(classify_lab(lab).color)];
    }
    return census;
}

int census_l1_distance(const ColorCensus& a, const ColorCensus& b) {
    int total = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        total += std::abs(a[i] - b[i]);
    }
    return total;
}

float centre_lab_distance(const CapturedFace& a, const CapturedFace& b) {
    const cv::Vec3f diff =
        a.lab[cube::model::center_facelet_index] - b.lab[cube::model::center_facelet_index];
    return std::sqrt(diff.dot(diff));
}

float registration_pair_score(const CapturedFace& a, const CapturedFace& b, const RoutingConfig& config) {
    return static_cast<float>(census_l1_distance(color_census(a), color_census(b))) +
           2.0F * centre_lab_distance(a, b) / config.content_equiv_lab;
}

CapturedFace relabel_capture(const CapturedFace& captured, cube::model::Face epoch_body_face,
                             int lattice_index) {
    const auto& map =
        kBodyRelabel[static_cast<std::size_t>(lattice_index)][static_cast<std::size_t>(epoch_body_face)];
    CapturedFace out;
    for (std::size_t k = 0; k < captured.lab.size(); ++k) {
        out.lab[static_cast<std::size_t>(map.cell[k])] = captured.lab[k];
    }
    return out;
}

float oriented_cell_lab_distance(const CapturedFace& epoch_face, const CapturedFace& stored,
                                 cube::model::Face epoch_body_face, int lattice_index) {
    const auto& map = kBodyRelabel[static_cast<std::size_t>(lattice_index)][static_cast<std::size_t>(epoch_body_face)];
    float total = 0.0F;
    int counted = 0;
    for (std::size_t cell = 0; cell < epoch_face.lab.size(); ++cell) {
        if (cell == cube::model::center_facelet_index) {
            continue;  // rotation-invariant: carries no roll information
        }
        const auto target = static_cast<std::size_t>(map.cell[cell]);
        const cv::Vec3f diff = epoch_face.lab[cell] - stored.lab[target];
        total += std::sqrt(diff.dot(diff));
        ++counted;
    }
    return counted > 0 ? total / static_cast<float>(counted) : 0.0F;
}

EpochRegistration register_epoch_gauge(const std::vector<EpochFaceSample>& epoch_faces,
                                       const std::array<std::optional<CapturedFace>, 6>& slots,
                                       const RoutingConfig& config) {
    EpochRegistration out;

    // Tight-re-association pins: an epoch face matching exactly ONE stored
    // slot fixes where every acceptable permutation must send it. Two or
    // more matches (a photometrically aliased pair) pin nothing - forcing
    // a choice there is exactly the wrong-face overwrite this design
    // forbids, so the ambiguity flows into the candidate scoring instead.
    struct Pin {
        Face epoch_face;
        Face store_face;
    };
    std::vector<Pin> pins;
    bool any_tight_match = false;
    for (const auto& sample : epoch_faces) {
        int matches = 0;
        Face matched{};
        for (std::size_t s = 0; s < slots.size(); ++s) {
            if (slots[s] && tight_match(sample.captured, *slots[s], config)) {
                ++matches;
                matched = static_cast<Face>(s);
            }
        }
        any_tight_match = any_tight_match || matches > 0;
        if (matches == 1) {
            pins.push_back({sample.epoch_face, matched});
        }
    }

    struct Candidate {
        int index = 0;
        float mean_score = std::numeric_limits<float>::max();
        int pairs = 0;
    };
    std::vector<Candidate> candidates;
    for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
        const auto& perm = kFacePermutation[static_cast<std::size_t>(g)];
        const bool satisfies_pins = std::ranges::all_of(pins, [&](const Pin& pin) {
            return perm[static_cast<std::size_t>(pin.epoch_face)] == pin.store_face;
        });
        if (!satisfies_pins) {
            continue;
        }
        float score = 0.0F;
        int pairs = 0;
        for (const auto& sample : epoch_faces) {
            const Face target = perm[static_cast<std::size_t>(sample.epoch_face)];
            const auto& slot = slots[static_cast<std::size_t>(target)];
            if (slot) {
                ++pairs;
                score += registration_pair_score(sample.captured, *slot, config);
            }
        }
        // Every pin-satisfying hypothesis with at least one induced pair
        // stays in the running here, HOWEVER badly it scores: the commit
        // bar applies to the winner only, further down. Filtering by the
        // bar before measuring the margin would silently discard a
        // runner-up scoring just above it (winner 5.5, runner-up 6.5,
        // margin 4) - exactly the near-miss the margin exists to refuse -
        // and commit the winner while a barely-worse hypothesis was still
        // alive.
        if (pairs > 0) {
            candidates.push_back({g, score / static_cast<float>(pairs), pairs});
        }
    }

    // Deterministic preference among equal scores: the identity relabel
    // first (the commonest physical reality - the user re-showed the cube
    // roughly as before), then the lowest index.
    std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
        if (a.mean_score != b.mean_score) {
            return a.mean_score < b.mean_score;
        }
        const bool a_id = a.index == kIdentityLatticeIndex;
        const bool b_id = b.index == kIdentityLatticeIndex;
        if (a_id != b_id) {
            return a_id;
        }
        return a.index < b.index;
    });

    out.candidates = static_cast<int>(candidates.size());
    if (!candidates.empty()) {
        out.best_score = candidates.front().mean_score;
        out.runner_up_score =
            candidates.size() > 1 ? candidates[1].mean_score : std::numeric_limits<float>::infinity();
    }

    if (candidates.empty() || candidates.front().mean_score > config.epoch_register_max_score) {
        // Distinguish "nothing recognised yet" (no epoch face tightly
        // matches any stored slot - the deferral that keeps a brand-new
        // face out of an empty slot until something is positively
        // recognised) from "evidence exists but contradicts itself"
        // (contradictory pins, every induced overlap scoring as a
        // conflict, or even the best hypothesis failing the commit bar).
        // Both defer; the status split is diagnostic.
        out.status = any_tight_match ? EpochRegistration::Status::ambiguous
                                     : EpochRegistration::Status::insufficient;
        return out;
    }

    const Candidate& best = candidates.front();
    std::vector<const Candidate*> tied;
    for (const Candidate& candidate : candidates) {
        // Margin only, deliberately: an over-bar member is still a LIVE
        // ALTERNATIVE hypothesis about where this epoch's faces belong,
        // and the margin exists precisely to refuse a winner it cannot
        // separate from one (winner 4.25, runner-up 6.25, bar 6, margin
        // 4). Filtering the bar in here would delete that runner-up and
        // commit the winner as `unique`. The bar's proper job is to gate
        // which member may WIN, and it is applied there - on the stage-1
        // roll winner below, and on candidates.front() above.
        if (candidate.mean_score - best.mean_score < config.epoch_register_margin) {
            tied.push_back(&candidate);
        }
    }
    out.tied = static_cast<int>(tied.size());
    if (tied.size() > 1) {
        // Stage 1 - oriented roll tie-break. The tied set is dominated by
        // the structural four-fold degeneracy: every element fixing a
        // given face scores identically under census and centre colour,
        // both rotation-invariant. Ordered stickers are not, so rank the
        // tied set by them and take the winner if it is absolutely
        // plausible, decisively better than the next, AND agrees on the
        // one signal the oriented comparison structurally cannot see.
        //
        // Computed for the tied set only: the value is consumed nowhere
        // else, so the recovery path's sqrt count follows the size of the
        // tie rather than all 24 pin-satisfying hypotheses.
        //
        // Why this may resolve without the stage-2 future-binding check
        // below, when stage 2 exists precisely because adopting a gauge
        // binds every FUTURE commit of the epoch. Stage 2 accepts a tie it
        // cannot break, so it must prove that no member's future
        // assignments could matter. Stage 1 does not accept a tie: it
        // BREAKS one, eliminating the other members on positive evidence
        // (an ordered-sticker disagreement beyond the margin is a
        // disagreement about the physical cube, not a preference), leaving
        // a single surviving hypothesis exactly as the `unique` basis
        // does - and `unique` binds the future on the same footing without
        // a further check. The whole weight of that argument rests on the
        // elimination being sound, which is why the winner must clear
        // three independent gates and not merely rank first.
        struct Roll {
            const Candidate* candidate = nullptr;
            float oriented_lab = 0.0F;
            // Every induced pair's centres agree in class and are inside
            // the tight re-association radius. oriented_cell_lab_distance
            // deliberately EXCLUDES the centre (it is rotation-invariant
            // and carries no roll information), which leaves the roll
            // ranking blind to the single strongest face-identity signal:
            // a DIFFERENT physical face whose eight-sticker ring happens
            // to alias the epoch face would post a near-zero roll while
            // its centre sat a whole colour class away. The score bar
            // bounds that only in aggregate - one pair at census 0 admits
            // a centre up to epoch_register_max_score * content_equiv_lab
            // / 2 = 36 Lab - so the roll winner is gated on the centre
            // directly. A candidate failing this is not re-ranked, it is
            // refused: the tie falls through to stage 2 unbroken.
            bool centres_agree = true;
        };
        std::vector<Roll> by_roll;
        by_roll.reserve(tied.size());
        for (const Candidate* candidate : tied) {
            const auto& perm = kFacePermutation[static_cast<std::size_t>(candidate->index)];
            float oriented = 0.0F;
            int pairs = 0;
            bool centres_agree = true;
            for (const auto& sample : epoch_faces) {
                const Face target = perm[static_cast<std::size_t>(sample.epoch_face)];
                const auto& slot = slots[static_cast<std::size_t>(target)];
                if (!slot) {
                    continue;
                }
                ++pairs;
                oriented +=
                    oriented_cell_lab_distance(sample.captured, *slot, sample.epoch_face, candidate->index);
                centres_agree = centres_agree &&
                                centre_lab_distance(sample.captured, *slot) < config.same_face_centre_lab &&
                                centre_class(sample.captured) == centre_class(*slot);
            }
            by_roll.push_back({candidate, pairs > 0 ? oriented / static_cast<float>(pairs) : 0.0F,
                               centres_agree});
        }
        std::ranges::sort(by_roll, [](const Roll& a, const Roll& b) {
            if (a.oriented_lab != b.oriented_lab) {
                return a.oriented_lab < b.oriented_lab;
            }
            return a.candidate->index < b.candidate->index;  // deterministic on exact ties
        });
        out.best_oriented_lab = by_roll.front().oriented_lab;
        out.runner_up_oriented_lab = by_roll[1].oriented_lab;
        // The bar applies to the candidate actually RETURNED, which the
        // roll sort may make someone other than candidates.front(). Note
        // this declines rather than ranking past an over-bar leader: if
        // the best roll belongs to a hypothesis the score rejected, the
        // roll evidence is pointing somewhere the score will not follow,
        // and that disagreement is ambiguity to be deferred - not licence
        // to hand the gauge to the second-best roll.
        if (by_roll.front().candidate->mean_score <= config.epoch_register_max_score &&
            by_roll.front().centres_agree &&
            by_roll.front().oriented_lab <= config.epoch_register_oriented_max_lab &&
            by_roll[1].oriented_lab - by_roll.front().oriented_lab >=
                config.epoch_register_oriented_margin_lab) {
            out.status = EpochRegistration::Status::resolved;
            out.basis = EpochRegistration::Basis::oriented;
            out.lattice_index = by_roll.front().candidate->index;
            return out;
        }

        // Stage 2 - content-equivalence tie-break: the tie is harmless exactly when
        // no choice among the tied permutations changes what any epoch
        // face would MEAN to assembly - each face's possible targets are
        // all empty (fills an interchangeable empty key) or all mutually
        // content-equivalent (updates interchangeable content). Checked
        // over ALL SIX epoch faces, not just the buffered ones: adopting a
        // gauge binds every FUTURE commit of this epoch too, and a
        // permutation pair that agrees on today's faces but files
        // tomorrow's onto materially different slots would turn the next
        // capture into a mis-route no later evidence could undo. Any mixed
        // or distinguishable case stays ambiguous and keeps buffering.
        for (std::size_t e = 0; e < 6; ++e) {
            std::vector<Face> targets;
            for (const Candidate* candidate : tied) {
                const Face target = kFacePermutation[static_cast<std::size_t>(candidate->index)][e];
                if (!std::ranges::contains(targets, target)) {
                    targets.push_back(target);
                }
            }
            const auto occupied = static_cast<std::size_t>(std::ranges::count_if(
                targets, [&](Face f) { return slots[static_cast<std::size_t>(f)].has_value(); }));
            if (occupied == 0) {
                continue;
            }
            if (occupied != targets.size()) {
                out.status = EpochRegistration::Status::ambiguous;
                return out;
            }
            for (std::size_t i = 0; i < targets.size(); ++i) {
                for (std::size_t j = i + 1; j < targets.size(); ++j) {
                    if (!content_equivalent(*slots[static_cast<std::size_t>(targets[i])],
                                            *slots[static_cast<std::size_t>(targets[j])], config)) {
                        out.status = EpochRegistration::Status::ambiguous;
                        return out;
                    }
                }
            }
        }
    }

    out.status = EpochRegistration::Status::resolved;
    out.basis = tied.size() > 1 ? EpochRegistration::Basis::content_equivalent
                                : EpochRegistration::Basis::unique;
    out.lattice_index = best.index;
    return out;
}

CaptureRouter::CaptureRouter(RoutingConfig config) : config_(config) {}

void CaptureRouter::begin_epoch_scope() {
    // New epoch = new body labels: the old gauge describes a dead frame.
    // Strikes and the layer-turn window are scoped to one locked segment -
    // conflicts against a track that no longer exists are not evidence
    // about the new one.
    gauge_known_ = false;
    recovering_ = filled_count() >= 2;
    epoch_faces_ = {};
    epoch_order_.clear();
    conflict_events_.clear();
    duplicate_quarantines_ = {};
    for (auto& slot : slots_) {
        if (slot) {
            slot->strikes = 0;
        }
    }
}

void CaptureRouter::on_epoch(int epoch) {
    if (epoch == current_epoch_) {
        return;
    }
    current_epoch_ = epoch;
    begin_epoch_scope();
}

void CaptureRouter::on_tracker_reset() {
    // -1 is "no epoch seen yet", so the tracker's restarted counter cannot
    // collide with it and the next on_epoch() is guaranteed to register.
    // The scope is dropped HERE rather than deferred to that call, so a
    // commit arriving in between can never be routed under the dead gauge.
    current_epoch_ = -1;
    begin_epoch_scope();
}

int CaptureRouter::fill_index_of(Face store_face) const {
    for (std::size_t i = 0; i < fill_order_.size(); ++i) {
        if (fill_order_[i] == store_face) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const CapturedFace* CaptureRouter::slot_at(int fill_index) const {
    if (fill_index < 0 || fill_index >= filled_count()) {
        return nullptr;
    }
    const auto& slot = slots_[static_cast<std::size_t>(fill_order_[static_cast<std::size_t>(fill_index)])];
    return slot ? &slot->captured : nullptr;
}

std::optional<Face> CaptureRouter::face_at(int fill_index) const {
    if (fill_index < 0 || fill_index >= filled_count()) {
        return std::nullopt;
    }
    return fill_order_[static_cast<std::size_t>(fill_index)];
}

int CaptureRouter::strikes_at(int fill_index) const {
    if (fill_index < 0 || fill_index >= filled_count()) {
        return 0;
    }
    const auto& slot = slots_[static_cast<std::size_t>(fill_order_[static_cast<std::size_t>(fill_index)])];
    return slot ? slot->strikes : 0;
}

void CaptureRouter::clear() {
    slots_ = {};
    fill_order_.clear();
    // A cleared router has seen no epoch: leaving the old number here would
    // make on_epoch() early-return against a restarted tracker counter and
    // skip registration entirely (see on_tracker_reset).
    current_epoch_ = -1;
    gauge_known_ = false;
    recovering_ = false;
    epoch_faces_ = {};
    epoch_order_.clear();
    conflict_events_.clear();
    duplicate_quarantines_ = {};
}

bool CaptureRouter::clear_slot(int fill_index) {
    if (fill_index < 0 || fill_index >= filled_count()) {
        return false;
    }
    const Face face = fill_order_[static_cast<std::size_t>(fill_index)];
    slots_[static_cast<std::size_t>(face)].reset();
    fill_order_.erase(fill_order_.begin() + fill_index);
    // A user-cleared slot's outstanding conflicts died with it - counting
    // them toward a later layer-turn diagnosis would double-charge the
    // face the user already resolved by hand.
    std::erase_if(conflict_events_, [face](const auto& event) { return event.first == face; });
    // Retakes during a re-orienting window can empty the store below what
    // registration needs: with <= 1 slot left, every hypothesis induces at
    // most one pair, so recovery could never resolve again this epoch and
    // commits would defer forever. But <= 1 slot is exactly on_epoch's own
    // "labels are relative" regime - there is no second slot for the choice
    // of permutation to be wrong against - so drop out of recovery
    // (buffered epoch faces included: they will re-converge and re-commit
    // within a pass or two) and let the next commit adopt a gauge directly.
    if (recovering_ && filled_count() <= 1) {
        recovering_ = false;
        epoch_faces_ = {};
        epoch_order_.clear();
    }
    return true;
}

bool CaptureRouter::adopt_gauge_direct(Face epoch_face, const CapturedFace& captured) {
    // The caller has established that no stored slot has a claim on this
    // capture - either at most one slot is filled (the epoch-change regime,
    // where the store's LABELS are simply relative: with one stored face
    // there is no second slot for a relabelling to contradict, and
    // cross-session evidence confirms sessions legitimately disagree on
    // names - slot F carries centre class green in live2, red in live3 and
    // blue in live4, three different physical faces under one name, by
    // design), or registration came back `insufficient`, which says the same
    // thing on evidence rather than by counting. The epoch therefore adopts
    // a gauge immediately instead of buffering.
    //
    // What is NOT relative is the ROLL, and that is what this function got
    // wrong. Four of the 24 permutations send a given epoch face to a given
    // store face; the old code took whichever came first in index order,
    // and both the label and the roll fell out of iteration. While the
    // write path ignored the gauge's cell component that was invisible -
    // not one of the three roll corruptions measured on the corpus came
    // through here. commit_direct now applies kBodyRelabel, so an arbitrary
    // roll would misfile all nine stickers on every such adoption, and
    // live3 alone ran 21 epochs with exactly one filled slot. The two fixes
    // had to land together, which is why the roll is now chosen from the
    // one piece of evidence this function always had and never read.
    if (fill_order_.empty()) {
        gauge_ = kIdentityLatticeIndex;
        return true;
    }

    // Which stored slots does this capture tightly re-associate with? With
    // one filled slot this is the old `tight_match(captured, front)` test
    // verbatim - fill_order_ holds exactly that one face - so nothing about
    // the epoch-change regime changes. It is written over the whole store so
    // that the function is correct for ANY store size, because that is what
    // the recovery path needs from it: comparing only fill_order_.front()
    // with two or more slots filled would let a re-show of the SECOND slot
    // be mistaken for a newcomer and filed beside itself.
    int matches = 0;
    Face matched_face{};
    for (const Face filled : fill_order_) {
        if (tight_match(captured, slots_[static_cast<std::size_t>(filled)]->captured, config_)) {
            ++matches;
            matched_face = filled;
        }
    }
    // Two stored slots both claim this reading (a photometrically aliased
    // pair). Unreachable in the <= 1 slot regime, and in the recovery regime
    // register_epoch_gauge has already declined to pin it for the same
    // reason: forcing a choice between claimants is exactly the wrong-face
    // overwrite this design forbids.
    if (matches > 1) {
        return false;
    }
    // The duplicate-centre near-miss - centre on top of a captured face
    // without the tight match that would authenticate it - measured against
    // EVERY filled slot, matching the check commit_direct runs on an empty
    // target (the centre cell is rotation-invariant, so testing it before the
    // relabel and after are the same test). Identical to the old single-slot
    // check when the store holds one face.
    if (matches == 0) {
        for (const Face filled : fill_order_) {
            if (centre_lab_distance(captured, slots_[static_cast<std::size_t>(filled)]->captured) <
                config_.duplicate_centre_radius) {
                return false;
            }
        }
    }

    // The one case with real evidence: the capture IS the stored face, so
    // it is about to overwrite that slot's nine cells and the roll decides
    // which sticker lands where. Rank the four admissible permutations by
    // ordered-sticker agreement - the same signal, through the same
    // function, that register_epoch_gauge's stage-1 tie-break uses, because
    // it is the only signal in the system that can tell rolls apart.
    //
    // No bar and no margin here, deliberately, and the asymmetry with
    // registration is the point: registration must decide WHETHER these
    // captures belong to this store at all, whereas tight_match has already
    // decided that (centre class, centre Lab and census all agree) and the
    // only open question is which of four rotations. One of the four is
    // right; refusing the best-fitting one would leave the alternative of
    // guessing, not of learning more. A rotationally symmetric face scores
    // all four identically - correctly, since then the stored bytes are the
    // same whichever wins - and the identity-first tie-break below picks
    // the commonest physical reality, matching register_epoch_gauge's own
    // preference.
    if (matches == 1) {
        const CapturedFace& stored = slots_[static_cast<std::size_t>(matched_face)]->captured;
        int best = -1;
        float best_lab = std::numeric_limits<float>::max();
        for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
            if (kFacePermutation[static_cast<std::size_t>(g)][static_cast<std::size_t>(epoch_face)] !=
                matched_face) {
                continue;
            }
            const float lab = oriented_cell_lab_distance(captured, stored, epoch_face, g);
            const bool better = lab < best_lab;
            const bool identity_breaks_tie =
                lab == best_lab && g == kIdentityLatticeIndex && best != kIdentityLatticeIndex;
            if (better || identity_breaks_tie) {
                best_lab = lab;
                best = g;
            }
        }
        if (best < 0) {
            return false;  // unreachable: four permutations always qualify
        }
        gauge_ = best;
        return true;
    }

    // A clearly different face. Nothing here constrains the gauge at all -
    // neither label nor roll - because the store holds only faces this epoch
    // has not recognised, so the relative geometry of them and the newcomer
    // is simply not observable yet. Stated plainly because it is a real
    // limit, not a solved problem: any permutation filing the newcomer
    // BESIDE rather than on top of a stored slot is as defensible as any
    // other, and requiring evidence instead cost 4 of the corpus's 11
    // captured slots when measured (live1 2->1, live2 3->1, live4 2->1) for
    // no measured safety gain, because the never-overwrite invariant is not
    // what is at stake on an empty target.
    //
    // "Beside" is the whole of the choice, and it is expressed here as
    // "onto an EMPTY store key" rather than "not onto fill_order_.front()".
    // Those are the same predicate while one slot is filled; with two or
    // more they are not, and the difference is load-bearing for any caller
    // arriving with a fuller store. Landing a never-seen face on an
    // OCCUPIED key would route it straight into commit_direct's verification
    // conflict - a quarantine, a strike, and force-lost at the second one -
    // which is the honest refusal for a wrong route but pure loss when an
    // empty key was available and carried no claim to contradict.
    //
    // Identity first, then lowest index. Identity is not evidence, it is
    // the likeliest physical reality (the user re-anchored holding the cube
    // roughly as before) and the same preference register_epoch_gauge
    // applies to its own score ties; the old loop simply took index 0, a
    // quarter turn about a face nobody was looking at.
    const auto lands_on_empty = [&](int lattice_index) {
        const Face target = kFacePermutation[static_cast<std::size_t>(lattice_index)]
                                            [static_cast<std::size_t>(epoch_face)];
        return !slots_[static_cast<std::size_t>(target)].has_value();
    };
    if (lands_on_empty(kIdentityLatticeIndex)) {
        gauge_ = kIdentityLatticeIndex;
        return true;
    }
    for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
        if (lands_on_empty(g)) {
            gauge_ = g;
            return true;
        }
    }
    // Only reachable with all six slots filled: there is no empty key left
    // to file a newcomer into, and the scan is complete anyway. Refusing
    // keeps the never-overwrite invariant unconditional here rather than
    // resting on the caller never asking.
    return false;
}

void CaptureRouter::buffer_epoch_face(Face epoch_face, const CapturedFace& captured) {
    if (!epoch_faces_[static_cast<std::size_t>(epoch_face)]) {
        epoch_order_.push_back(epoch_face);
    }
    epoch_faces_[static_cast<std::size_t>(epoch_face)] = captured;
}

CaptureRouter::DirectCommit CaptureRouter::commit_direct(Face epoch_face, const CapturedFace& captured,
                                                         std::int64_t now_ms, CommitResult& result) {
    const Face store_face = kFacePermutation[static_cast<std::size_t>(gauge_)][static_cast<std::size_t>(epoch_face)];
    // Into the STORE's frame before anything is compared or written. The
    // gauge is a whole-cube rotation between two body labellings, so it
    // moves stickers as well as face names; kFacePermutation above gave the
    // name, this gives the nine cells (see relabel_capture's note for the
    // three measured corruptions that came of storing the epoch-frame array
    // verbatim). Both verification terms below are rotation-invariant, so
    // relabelling cannot change ANY routing decision - it only makes the
    // bytes that land match the slot they land in.
    const CapturedFace stored_frame = relabel_capture(captured, epoch_face, gauge_);
    auto& slot = slots_[static_cast<std::size_t>(store_face)];

    if (slot) {
        if (now_ms - slot->last_commit_ms < config_.recommit_holdoff_ms) {
            return {CommitOutcome::holdoff};
        }
        // The verified in-place update - the ONLY overwrite path in the
        // system. Identity routed the capture here; centre class, centre
        // Lab and census must all agree it is the same physical face, or
        // nothing is written.
        if (verified_same_face(stored_frame, slot->captured, config_)) {
            slot->captured = stored_frame;
            slot->strikes = 0;
            slot->last_commit_ms = now_ms;
            // A verified re-show also clears the face's outstanding
            // conflict evidence: a slip that resolved itself must not keep
            // counting toward a layer-turn diagnosis.
            std::erase_if(conflict_events_,
                          [store_face](const auto& event) { return event.first == store_face; });
            return {CommitOutcome::updated, fill_index_of(store_face)};
        }
        // Verification conflict. One face conflicting is more likely a
        // tracking slip (strikes -> force-lost, slots preserved: they were
        // verified when written); conflicts on TWO distinct faces inside
        // the window mean the stickers themselves changed - a mid-scan
        // layer turn - and every capture predating it is unusable.
        conflict_events_.emplace_back(store_face, now_ms);
        std::erase_if(conflict_events_, [&](const auto& event) {
            return now_ms - event.second > config_.layer_turn_window_ms;
        });
        std::vector<Face> distinct;
        for (const auto& event : conflict_events_) {
            if (!std::ranges::contains(distinct, event.first)) {
                distinct.push_back(event.first);
            }
        }
        if (static_cast<int>(distinct.size()) >= config_.layer_turn_conflict_faces) {
            result.layer_turn = true;
            clear();
            return {CommitOutcome::quarantined};
        }
        ++slot->strikes;
        if (slot->strikes >= config_.conflict_strikes) {
            result.force_lost = true;
        }
        return {CommitOutcome::quarantined};
    }

    // Empty target. Duplicate-centre check first: the tracked identity
    // claims a NEW face while its centre colour sits on top of a captured
    // one - slip evidence, and writing it would seed the exact duplicate-
    // slot wedge the legacy store suffered. Quarantine instead, and make
    // the slip evidence COUNT (identity-policy rule 3: "belief
    // penalized"): a wrong gauge adopted at an epoch boundary produces
    // exactly this signature on every pass and never trips the occupied-
    // slot strike path, so repeated duplicate quarantines on one claimed
    // face escalate to force-lost just like verification strikes do - the
    // honest way to penalize a belief the router cannot reach directly.
    for (const Face filled : fill_order_) {
        if (centre_lab_distance(stored_frame, slots_[static_cast<std::size_t>(filled)]->captured) <
            config_.duplicate_centre_radius) {
            result.duplicate_centre = true;
            if (++duplicate_quarantines_[static_cast<std::size_t>(store_face)] >=
                config_.conflict_strikes) {
                result.force_lost = true;
            }
            return {CommitOutcome::quarantined};
        }
    }
    slot = Slot{.captured = stored_frame, .strikes = 0, .last_commit_ms = now_ms};
    fill_order_.push_back(store_face);
    return {CommitOutcome::filled, filled_count() - 1};
}

CommitResult CaptureRouter::commit(Face epoch_face, const CapturedFace& captured, std::int64_t now_ms) {
    CommitResult result;

    if (!gauge_known_) {
        if (recovering_) {
            buffer_epoch_face(epoch_face, captured);
            std::vector<EpochFaceSample> samples;
            samples.reserve(epoch_order_.size());
            std::array<std::optional<CapturedFace>, 6> store_view{};
            for (const Face face : fill_order_) {
                store_view[static_cast<std::size_t>(face)] = slots_[static_cast<std::size_t>(face)]->captured;
            }
            for (const Face face : epoch_order_) {
                samples.push_back({face, *epoch_faces_[static_cast<std::size_t>(face)]});
            }
            const EpochRegistration registration = register_epoch_gauge(samples, store_view, config_);
            result.registration = registration;
            if (registration.status == EpochRegistration::Status::resolved) {
                gauge_ = registration.lattice_index;
                gauge_known_ = true;
                recovering_ = false;
                epoch_faces_ = {};
                epoch_order_.clear();
                // Flush every buffered face in arrival order through the
                // normal policy - registration chose WHERE they go, the
                // commit rules still decide WHETHER each may land.
                for (const auto& sample : samples) {
                    if (result.layer_turn) {
                        break;  // the store just self-cleared; nothing else may land
                    }
                    const DirectCommit direct =
                        commit_direct(sample.epoch_face, sample.captured, now_ms, result);
                    if (direct.slot_index >= 0) {
                        result.confirmed_slots.push_back(direct.slot_index);
                    }
                    if (sample.epoch_face == epoch_face) {
                        result.outcome = direct.outcome;
                    }
                }
                return result;
            }
            // `ambiguous` keeps buffering, unconditionally. That status
            // means evidence EXISTS and contradicts itself - contradictory
            // pins, an aliased pair, a tie the roll could not break - and it
            // is the status that is measurably working: over runA..runD the
            // true re-shows posted best-roll 2.0-10.2 mean Lab against false
            // aliases at 38-56, so a deferral here is the router declining a
            // choice it could genuinely get wrong.
            if (registration.status != EpochRegistration::Status::insufficient) {
                result.outcome = CommitOutcome::deferred;
                return result;
            }
            // `insufficient` is the opposite situation and had been sharing
            // ambiguous's response for want of anywhere else to go. It means
            // any_tight_match == false: not one buffered epoch face resembles
            // ANY stored slot, so there is no hypothesis to be wrong about -
            // this is a face the scan has never captured, and registration
            // structurally cannot admit it. register_epoch_gauge only scores
            // candidates with pairs > 0, i.e. permutations mapping a visible
            // epoch face onto an ALREADY-FILLED slot, so a never-stored face
            // is always scored against the wrong stored face: measured over
            // runA..runD at 9.8-16.1 against a 6.0 commit bar, insufficient
            // 40 times in 79 registration attempts, and the reason all four
            // clips ceilinged at two filled slots while 70-85% of each run
            // sat in recovery. runD is the sharpest case - four geom_conf 1.0
            // nine-of-nine reads at passes 273-286, every one refused.
            //
            // So treat it as what it is, a NEW FACE, and hand it to the
            // ordinary <= 1-slot admission path rather than to a registration
            // that has nothing to register. This is not new policy: it is
            // adopt_gauge_direct's own documented rule for an unrecognised
            // face ("the never-overwrite invariant is not what is at stake on
            // an empty target ... requiring evidence instead cost 4 of the
            // corpus's 11 captured slots"), which recovering mode never
            // inherited. Nothing about the invariant is weakened, because
            // nothing about the WRITE changes: adopt_gauge_direct only ever
            // picks a gauge landing this face on an EMPTY key, and
            // commit_direct still runs the duplicate-centre scan over every
            // filled slot before writing and still refuses to overwrite an
            // occupied one except through the verified in-place update.
            if (!adopt_gauge_direct(epoch_face, captured)) {
                // Refused: the newcomer's centre sits on a captured face's
                // without authenticating as it (or the store is full). That
                // is slip evidence, not a new face - keep deferring, and keep
                // the registration diagnostic that explains why.
                result.outcome = CommitOutcome::deferred;
                return result;
            }
            gauge_known_ = true;
            recovering_ = false;
            // The other buffered faces are dropped rather than flushed under
            // this gauge. The gauge was chosen from ONE unrecognised face and
            // is arbitrary for the rest, so flushing them would be a guess
            // several slots wide; they re-converge and re-commit within a
            // pass or two (the argument clear_slot() already makes), by which
            // time gauge_known_ routes them directly and each is judged on
            // its own duplicate-centre evidence.
            epoch_faces_ = {};
            epoch_order_.clear();
        } else if (!adopt_gauge_direct(epoch_face, captured)) {
            // The adopt-time refusal is the same duplicate-centre
            // signature (near-miss centre on the one stored slot without a
            // tight match); no counter here - the gauge stays unknown, so
            // each retry re-tests genuinely fresh evidence rather than
            // re-accusing a fixed claimed face.
            result.outcome = CommitOutcome::quarantined;
            result.duplicate_centre = true;
            return result;
        } else {
            gauge_known_ = true;
        }
    }

    const DirectCommit direct = commit_direct(epoch_face, captured, now_ms, result);
    result.outcome = direct.outcome;
    if (direct.slot_index >= 0) {
        result.confirmed_slots.push_back(direct.slot_index);
    }
    return result;
}

}  // namespace cube::vision
