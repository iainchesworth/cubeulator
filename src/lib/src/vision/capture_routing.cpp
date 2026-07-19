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
    return centre_lab_distance(a, b) < config.reassoc_centre_lab && centre_class(a) == centre_class(b) &&
           census_l1_distance(color_census(a), color_census(b)) <= config.reassoc_census;
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
    std::vector<Candidate> eligible;
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
        if (pairs > 0) {
            const float mean = score / static_cast<float>(pairs);
            if (mean <= config.epoch_register_max_score) {
                eligible.push_back({g, mean, pairs});
            }
        }
    }

    if (eligible.empty()) {
        // Distinguish "nothing recognised yet" (no epoch face tightly
        // matches any stored slot - the deferral that keeps a brand-new
        // face out of an empty slot until something is positively
        // recognised) from "evidence exists but contradicts itself"
        // (contradictory pins, or every induced overlap scores as a
        // conflict). Both defer; the status split is diagnostic.
        out.status = any_tight_match ? EpochRegistration::Status::ambiguous
                                     : EpochRegistration::Status::insufficient;
        return out;
    }

    // Deterministic preference among equal scores: the identity relabel
    // first (the commonest physical reality - the user re-showed the cube
    // roughly as before), then the lowest index.
    std::ranges::sort(eligible, [](const Candidate& a, const Candidate& b) {
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
    const Candidate& best = eligible.front();
    std::vector<const Candidate*> tied;
    for (const Candidate& candidate : eligible) {
        if (candidate.mean_score - best.mean_score < config.epoch_register_margin) {
            tied.push_back(&candidate);
        }
    }
    if (tied.size() > 1) {
        // Content-equivalence tie-break: the tie is harmless exactly when
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
    out.lattice_index = best.index;
    return out;
}

CaptureRouter::CaptureRouter(RoutingConfig config) : config_(config) {}

void CaptureRouter::on_epoch(int epoch) {
    if (epoch == current_epoch_) {
        return;
    }
    current_epoch_ = epoch;
    // New epoch = new body labels: the old gauge describes a dead frame.
    // Strikes and the layer-turn window are scoped to one locked segment -
    // conflicts against a track that no longer exists are not evidence
    // about the new one.
    gauge_known_ = false;
    recovering_ = filled_count() >= 2;
    epoch_faces_ = {};
    epoch_order_.clear();
    conflict_events_.clear();
    for (auto& slot : slots_) {
        if (slot) {
            slot->strikes = 0;
        }
    }
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
    gauge_known_ = false;
    recovering_ = false;
    epoch_faces_ = {};
    epoch_order_.clear();
    conflict_events_.clear();
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
    return true;
}

bool CaptureRouter::adopt_gauge_direct(Face epoch_face, const CapturedFace& captured) {
    // At most one slot is filled: identity is definitional (there is no
    // second slot for the choice of permutation to be wrong against), so
    // the epoch adopts a gauge immediately instead of buffering. The one
    // stored slot still gets its say: a tight content match re-keys it
    // under the new frame, a near-miss centre (could be the same face,
    // colour drifted) refuses adoption so nothing lands on a guess, and a
    // clearly different face picks any permutation that files the new
    // capture beside - not on top of - the stored one.
    if (fill_order_.empty()) {
        gauge_ = kIdentityLatticeIndex;
        return true;
    }
    const Face stored_face = fill_order_.front();
    const CapturedFace& stored = slots_[static_cast<std::size_t>(stored_face)]->captured;
    const bool matches = tight_match(captured, stored, config_);
    if (!matches && centre_lab_distance(captured, stored) < config_.duplicate_centre_radius) {
        return false;
    }
    const Face wanted = matches ? stored_face : Face{};
    for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
        const Face target = kFacePermutation[static_cast<std::size_t>(g)][static_cast<std::size_t>(epoch_face)];
        if (matches ? target == wanted : target != stored_face) {
            gauge_ = g;
            return true;
        }
    }
    return false;  // unreachable: the 24 permutations cover every mapping
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
    auto& slot = slots_[static_cast<std::size_t>(store_face)];

    if (slot) {
        if (now_ms - slot->last_commit_ms < config_.recommit_holdoff_ms) {
            return {CommitOutcome::holdoff};
        }
        // The verified in-place update - the ONLY overwrite path in the
        // system. Identity routed the capture here; centre and census must
        // both agree it is the same physical face, or nothing is written.
        if (centre_lab_distance(captured, slot->captured) <= config_.verify_centre_lab_radius &&
            census_l1_distance(color_census(captured), color_census(slot->captured)) <=
                config_.verify_census_drift) {
            slot->captured = captured;
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
    // slot wedge the legacy store suffered. Quarantine instead.
    for (const Face filled : fill_order_) {
        if (centre_lab_distance(captured, slots_[static_cast<std::size_t>(filled)]->captured) <
            config_.duplicate_centre_radius) {
            return {CommitOutcome::quarantined};
        }
    }
    slot = Slot{.captured = captured, .strikes = 0, .last_commit_ms = now_ms};
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
            if (registration.status != EpochRegistration::Status::resolved) {
                result.outcome = CommitOutcome::deferred;
                return result;
            }
            gauge_ = registration.lattice_index;
            gauge_known_ = true;
            recovering_ = false;
            epoch_faces_ = {};
            epoch_order_.clear();
            // Flush every buffered face in arrival order through the
            // normal policy - registration chose WHERE they go, the commit
            // rules still decide WHETHER each may land.
            for (const auto& sample : samples) {
                if (result.layer_turn) {
                    break;  // the store just self-cleared; nothing else may land
                }
                const DirectCommit direct = commit_direct(sample.epoch_face, sample.captured, now_ms, result);
                if (direct.slot_index >= 0) {
                    result.confirmed_slots.push_back(direct.slot_index);
                }
                if (sample.epoch_face == epoch_face) {
                    result.outcome = direct.outcome;
                }
            }
            return result;
        }
        if (!adopt_gauge_direct(epoch_face, captured)) {
            result.outcome = CommitOutcome::quarantined;
            return result;
        }
        gauge_known_ = true;
    }

    const DirectCommit direct = commit_direct(epoch_face, captured, now_ms, result);
    result.outcome = direct.outcome;
    if (direct.slot_index >= 0) {
        result.confirmed_slots.push_back(direct.slot_index);
    }
    return result;
}

}  // namespace cube::vision
