#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/orientation_lattice.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

using namespace cube::vision;
using cube::model::Face;
using cube::model::FacetColor;

namespace {

// ---------------------------------------------------------------------------
// Adversarial routing harness. Contents are chosen against classify_lab's
// REAL hue-band behaviour (color_resolution.cpp) - the aliases below are
// the live failure modes the router exists to survive: an over-exposed
// orange face whose hue drifts under the 44-degree red boundary reads as a
// solid red face with a centre a few Lab from the true red slot's, which
// is exactly the reading that used to execute a silent wrong-face
// overwrite.
// ---------------------------------------------------------------------------

// Distinct solid faces, one per colour class, centres far apart pairwise.
const cv::Vec3f kWhite{96.0F, 2.0F, 2.0F};      // chroma < 15
const cv::Vec3f kYellow{88.0F, 5.0F, 80.0F};    // hue ~86
const cv::Vec3f kRed{45.0F, 60.0F, 35.0F};      // hue ~30
const cv::Vec3f kOrange{65.0F, 40.0F, 57.0F};   // hue ~55
const cv::Vec3f kGreen{50.0F, -50.0F, 45.0F};   // hue ~138
const cv::Vec3f kBlue{35.0F, 12.0F, -60.0F};    // hue ~281
// A washed orange tile: hue 36 (classifies RED), centre only ~8.7 Lab from
// kRed - inside the duplicate radius, same class, identical solid census.
const cv::Vec3f kWashedOrangeAsRed{50.0F, 55.0F, 40.0F};

[[nodiscard]] CapturedFace solid(const cv::Vec3f& lab) {
    CapturedFace face;
    face.lab.fill(lab);
    return face;
}

// Same face re-shown under drifted lighting: centre nudged (well inside
// the verify radius), census unchanged.
[[nodiscard]] CapturedFace drifted(const CapturedFace& face, float delta_l) {
    CapturedFace out = face;
    for (auto& lab : out.lab) {
        lab[0] += delta_l;
    }
    return out;
}

// One sticker swapped to a different class - census L1 moves by 2 per swap.
[[nodiscard]] CapturedFace with_sticker(const CapturedFace& face, std::size_t cell, const cv::Vec3f& lab) {
    CapturedFace out = face;
    out.lab[cell] = lab;
    return out;
}

}  // namespace

TEST_CASE("census helpers count sticker populations rotation-invariantly", "[routing]") {
    const CapturedFace red = solid(kRed);
    const auto red_census = color_census(red);
    CHECK(red_census[static_cast<std::size_t>(FacetColor::red)] == 9);
    CHECK(census_l1_distance(red_census, red_census) == 0);

    // One sticker reclassified costs 2 (one out of its bucket, one into
    // another).
    const auto one_swap = color_census(with_sticker(red, 0, kGreen));
    CHECK(census_l1_distance(red_census, one_swap) == 2);

    // Solid faces of different colours are maximally apart.
    CHECK(census_l1_distance(red_census, color_census(solid(kOrange))) == 18);

    // The adversarial alias: a washed orange face's census is IDENTICAL to
    // the red face's - census cannot tell them apart, which is why it only
    // ever verifies and never routes.
    CHECK(census_l1_distance(red_census, color_census(solid(kWashedOrangeAsRed))) == 0);
    CHECK(centre_lab_distance(red, solid(kWashedOrangeAsRed)) < 18.0F);
}

TEST_CASE("distinct faces fill six identity-keyed slots in fill order", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    const std::array<cv::Vec3f, 6> palette{kWhite, kYellow, kRed, kOrange, kGreen, kBlue};
    std::int64_t t = 0;
    for (std::size_t f = 0; f < palette.size(); ++f) {
        t += 1000;
        const auto result = router.commit(static_cast<Face>(f), solid(palette[f]), t);
        CHECK(result.outcome == CommitOutcome::filled);
        REQUIRE(result.confirmed_slots.size() == 1);
        CHECK(result.confirmed_slots.front() == static_cast<int>(f));
        CHECK_FALSE(result.layer_turn);
        CHECK_FALSE(result.force_lost);
    }
    CHECK(router.filled_count() == 6);
    // Fill order preserved: slot i still holds the i-th committed face.
    for (std::size_t f = 0; f < palette.size(); ++f) {
        REQUIRE(router.slot_at(static_cast<int>(f)) != nullptr);
        CHECK(centre_lab_distance(*router.slot_at(static_cast<int>(f)), solid(palette[f])) == 0.0F);
    }
}

TEST_CASE("verified in-place update is the only content overwrite", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, solid(kRed), 0).outcome == CommitOutcome::filled);

    SECTION("a drifted re-show of the same face refreshes the slot") {
        const CapturedFace reshow = drifted(with_sticker(solid(kRed), 2, kGreen), 6.0F);
        const auto result = router.commit(Face::F, reshow, 2000);
        CHECK(result.outcome == CommitOutcome::updated);
        REQUIRE(result.confirmed_slots.size() == 1);
        CHECK(result.confirmed_slots.front() == 0);
        CHECK(centre_lab_distance(*router.slot_at(0), reshow) == 0.0F);
    }

    SECTION("identity routing a different face's colours there quarantines") {
        const auto result = router.commit(Face::F, solid(kGreen), 2000);
        CHECK(result.outcome == CommitOutcome::quarantined);
        CHECK_FALSE(result.force_lost);
        // Slot content untouched, conflict recorded.
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
        CHECK(router.strikes_at(0) == 1);
    }

    SECTION("the per-face holdoff pauses even a verified re-commit") {
        const auto early = router.commit(Face::F, drifted(solid(kRed), 3.0F), 400);
        CHECK(early.outcome == CommitOutcome::holdoff);
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
        const auto later = router.commit(Face::F, drifted(solid(kRed), 3.0F), 900);
        CHECK(later.outcome == CommitOutcome::updated);
    }
}

TEST_CASE("solved-cube centre alias quarantines instead of overwriting", "[routing]") {
    // The primary reported bug, replayed against the new store: red is
    // captured; a DIFFERENT physical face (tracked as body U) washes out
    // to a solid red reading whose centre sits on top of the red slot's.
    // The legacy centre-keyed store executed slots_[red] = orange here.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::R, solid(kRed), 0).outcome == CommitOutcome::filled);

    const auto result = router.commit(Face::U, solid(kWashedOrangeAsRed), 2000);
    CHECK(result.outcome == CommitOutcome::quarantined);
    CHECK(result.duplicate_centre);
    CHECK(result.confirmed_slots.empty());
    // ZERO wrong-face commits: the red slot still holds red, and no new
    // slot was written either.
    CHECK(router.filled_count() == 1);
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
}

TEST_CASE("two strikes on one face force-lose the track with slots preserved", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::R, solid(kRed), 0).outcome == CommitOutcome::filled);

    const auto first = router.commit(Face::R, solid(kGreen), 2000);
    CHECK(first.outcome == CommitOutcome::quarantined);
    CHECK_FALSE(first.force_lost);
    CHECK(router.strikes_at(0) == 1);

    const auto second = router.commit(Face::R, solid(kGreen), 4000);
    CHECK(second.outcome == CommitOutcome::quarantined);
    CHECK(second.force_lost);
    CHECK_FALSE(second.layer_turn);
    // Slots preserved - they were verified when written; the TRACK is the
    // suspect.
    CHECK(router.filled_count() == 1);
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);

    // The re-anchor that follows starts a new epoch; strikes are scoped to
    // the dead locked segment.
    router.on_epoch(2);
    CHECK(router.strikes_at(0) == 0);
}

TEST_CASE("census conflicts on two distinct faces diagnose a layer turn", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::F, solid(kGreen), 2000).outcome == CommitOutcome::filled);

    SECTION("two conflicting faces inside the window clear every capture") {
        CHECK(router.commit(Face::U, solid(kBlue), 5000).outcome == CommitOutcome::quarantined);
        const auto second = router.commit(Face::R, solid(kYellow), 6000);
        CHECK(second.layer_turn);
        // Full capture reset - the sanctioned response to the static-
        // stickers premise being violated.
        CHECK(router.filled_count() == 0);
    }

    SECTION("conflicts further apart than the window stay independent strikes") {
        CHECK(router.commit(Face::U, solid(kBlue), 5000).outcome == CommitOutcome::quarantined);
        const auto late = router.commit(Face::R, solid(kYellow), 11000);
        CHECK(late.outcome == CommitOutcome::quarantined);
        CHECK_FALSE(late.layer_turn);
        CHECK(router.filled_count() == 3);
    }

    SECTION("a verified re-show clears its face's conflict evidence") {
        CHECK(router.commit(Face::U, solid(kBlue), 5000).outcome == CommitOutcome::quarantined);
        // U verifies fine on the next commit - the earlier conflict was a
        // slip, not a layer turn...
        CHECK(router.commit(Face::U, drifted(solid(kWhite), 3.0F), 6000).outcome == CommitOutcome::updated);
        // ...so a later conflict on R alone must NOT complete a layer-turn
        // pair with the withdrawn U event.
        const auto conflict = router.commit(Face::R, solid(kYellow), 7000);
        CHECK(conflict.outcome == CommitOutcome::quarantined);
        CHECK_FALSE(conflict.layer_turn);
        CHECK(router.filled_count() == 3);
    }
}

TEST_CASE("property: slot censuses never change beyond verify drift without escalation", "[routing]") {
    // The invariant, stated as the plan's property test: NO capture
    // sequence may change a slot's census by more than kVerifyCensusDrift
    // unless the change went through quarantine/invalidation (i.e. content
    // only ever changes via the verified update, which bounds the drift).
    CaptureRouter router;
    const RoutingConfig config;
    router.on_epoch(1);
    std::mt19937 rng(20260720);
    const std::array<cv::Vec3f, 8> pool{kWhite, kYellow, kRed,  kOrange,
                                        kGreen, kBlue,   kWashedOrangeAsRed, cv::Vec3f{70.0F, 30.0F, 40.0F}};
    std::uniform_int_distribution<std::size_t> pick_face(0, 5);
    std::uniform_int_distribution<std::size_t> pick_colour(0, pool.size() - 1);
    std::uniform_int_distribution<int> pick_dt(0, 1200);
    std::uniform_int_distribution<int> pick_swaps(0, 2);

    std::int64_t t = 0;
    for (int iteration = 0; iteration < 500; ++iteration) {
        // Snapshot the store, keyed by fill index.
        std::vector<ColorCensus> before;
        for (int i = 0; i < router.filled_count(); ++i) {
            before.push_back(color_census(*router.slot_at(i)));
        }

        CapturedFace captured = solid(pool[pick_colour(rng)]);
        for (int swap = pick_swaps(rng); swap > 0; --swap) {
            captured = with_sticker(captured, static_cast<std::size_t>(pick_colour(rng) % 9),
                                    pool[pick_colour(rng)]);
        }
        t += pick_dt(rng);
        const auto result = router.commit(static_cast<Face>(pick_face(rng)), captured, t);

        if (result.layer_turn) {
            CHECK(router.filled_count() == 0);  // sanctioned full reset
            continue;
        }
        // Every surviving pre-existing slot: content either untouched or
        // changed within the verified-update drift bound.
        for (std::size_t i = 0; i < before.size(); ++i) {
            REQUIRE(static_cast<int>(i) < router.filled_count());
            const int drift = census_l1_distance(before[i], color_census(*router.slot_at(static_cast<int>(i))));
            CHECK(drift <= config.verify_census_drift);
        }
    }
}

TEST_CASE("epoch recovery re-registers a permuted epoch by content", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::F, solid(kGreen), 2000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());

    SECTION("corner-triple correspondence resolves once two faces pin it") {
        // The regrip rotated the cube 120 degrees about the URF corner:
        // physical white (stored at U) now tracks as body F, red (stored
        // R) as body U, green (stored F) as body R - the face 3-cycle a
        // real rotation produces.
        const auto first = router.commit(Face::F, drifted(solid(kWhite), 3.0F), 10000);
        // One pinned face cannot resolve the gauge: the tied candidates
        // would file OTHER faces onto materially different slots, so
        // nothing may commit yet.
        CHECK(first.outcome == CommitOutcome::deferred);
        CHECK(first.confirmed_slots.empty());
        CHECK(router.reorienting());
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kWhite)) == 0.0F);

        const auto second = router.commit(Face::U, drifted(solid(kRed), 3.0F), 11000);
        // Two pins determine the rotation uniquely: both buffered faces
        // flush onto their true slots as verified updates.
        CHECK(second.outcome == CommitOutcome::updated);
        REQUIRE(second.confirmed_slots.size() == 2);
        CHECK_FALSE(router.reorienting());
        CHECK(router.filled_count() == 3);
        CHECK(centre_lab_distance(*router.slot_at(0), drifted(solid(kWhite), 3.0F)) == 0.0F);
        CHECK(centre_lab_distance(*router.slot_at(1), drifted(solid(kRed), 3.0F)) == 0.0F);

        // And the resolved gauge routes the epoch's remaining faces
        // directly: green (body R this epoch) lands on its stored slot.
        const auto third = router.commit(Face::R, drifted(solid(kGreen), 3.0F), 12000);
        CHECK(third.outcome == CommitOutcome::updated);
        CHECK(centre_lab_distance(*router.slot_at(2), drifted(solid(kGreen), 3.0F)) == 0.0F);
    }

    SECTION("two epoch faces claiming the same stored slot are refused") {
        // Glare washes TWO different epoch faces to readings that both
        // tightly match the stored white slot. The pins contradict (a
        // permutation cannot send two faces to one slot), the candidate
        // set goes empty, and NOTHING commits - the router refuses to pick
        // a claimant, however good each single match looks.
        CHECK(router.commit(Face::F, drifted(solid(kWhite), 3.0F), 10000).outcome == CommitOutcome::deferred);
        const auto second = router.commit(Face::D, drifted(solid(kWhite), 5.0F), 11000);
        CHECK(second.outcome == CommitOutcome::deferred);
        CHECK(router.reorienting());
        CHECK(router.filled_count() == 3);
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kWhite)) == 0.0F);
    }

    SECTION("a brand-new face is admitted straight into an empty slot") {
        // BEHAVIOUR CHANGE, and the whole coverage repair. This section
        // used to assert that blue DEFERRED here and only landed once
        // other faces were positively re-associated. That was never a
        // safety property - it was register_epoch_gauge's shape leaking
        // into policy. Registration scores only permutations that map a
        // visible epoch face onto an ALREADY-FILLED slot, so a face the
        // scan has never captured is always scored against the wrong
        // stored face and can never clear the 6.0 commit bar (measured
        // 9.8-16.1 over runA..runD). Waiting for "other faces to be
        // recognised" therefore meant waiting for something that only
        // happens on a re-show, which is why all four live clips ceilinged
        // at exactly two filled slots with 70-85% of each run in recovery.
        //
        // The never-overwrite invariant is untouched here and is asserted
        // below: blue can only land on an EMPTY key, and all three
        // captured slots keep their content.
        const auto unknown = router.commit(Face::D, solid(kBlue), 10000);
        CHECK(unknown.outcome == CommitOutcome::filled);
        REQUIRE(unknown.registration.has_value());
        CHECK(unknown.registration->status == EpochRegistration::Status::insufficient);
        CHECK(router.filled_count() == 4);
        // Still re-orienting: admitting a newcomer fills a key, it does not
        // resolve the epoch. The gauge that placed blue was chosen with no
        // evidence at all, so it is used for this one empty-target write and
        // then discarded rather than promoted to govern the rest of the
        // epoch (see the runD 180-degree overwrite in commit()'s comment).
        CHECK(router.reorienting());
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kWhite)) == 0.0F);
        CHECK(centre_lab_distance(*router.slot_at(1), solid(kRed)) == 0.0F);
        CHECK(centre_lab_distance(*router.slot_at(2), solid(kGreen)) == 0.0F);
        CHECK(centre_lab_distance(*router.slot_at(3), solid(kBlue)) == 0.0F);
    }
}

TEST_CASE("content-equivalent slots break registration ties harmlessly", "[routing]") {
    // Solved-cube termination guarantee: red and orange washed to
    // IDENTICAL censuses with centres inside the resolver's own seed
    // floor, stored on opposite slots (R and L), likewise the green pair
    // on F and B. A one-pin recovery leaves a 180-degree ambiguity about
    // the pinned axis - but every difference between the tied candidates
    // maps equivalent content onto equivalent content, so registration
    // picks one deterministically instead of wedging.
    //
    // The duplicate-centre rule (correctly) refuses to FILL two slots this
    // similar, so equivalent pairs only ever arise the way they do live:
    // filled far enough apart, then pulled together by verified re-show
    // updates as the lighting drifts.
    const CapturedFace red_a = solid(kRed);
    const CapturedFace red_b = drifted(solid(kRed), 8.0F);   // centres 8 apart: equivalent
    const CapturedFace green_a = solid(kGreen);
    const CapturedFace green_b = drifted(solid(kGreen), 8.0F);

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, red_a, 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::L, drifted(solid(kRed), 20.0F), 2000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::L, red_b, 3000).outcome == CommitOutcome::updated);
    REQUIRE(router.commit(Face::F, green_a, 4000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::B, drifted(solid(kGreen), 20.0F), 5000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::B, green_b, 6000).outcome == CommitOutcome::updated);

    router.on_epoch(2);
    REQUIRE(router.reorienting());
    // White pins U; the red probe matches BOTH red slots (the aliased
    // pair), pinning nothing - the residual candidates differ only by the
    // 180-degree turn that swaps R/L and F/B, all equivalent pairs.
    CHECK(router.commit(Face::U, drifted(solid(kWhite), 3.0F), 10000).outcome == CommitOutcome::deferred);
    const auto resolved = router.commit(Face::R, drifted(red_a, 2.0F), 11000);
    CHECK(resolved.outcome == CommitOutcome::updated);
    CHECK_FALSE(router.reorienting());
    CHECK(router.filled_count() == 5);
}

TEST_CASE("an aliased pair with distinguishable content refuses to guess", "[routing]") {
    // Same shape as the tie-break case, but the two red-ish slots are NOT
    // content-equivalent (centres past the equivalence floor): picking
    // either would materially decide which face gets overwritten later, so
    // registration must stay ambiguous.
    const CapturedFace red_a = solid(kRed);
    const CapturedFace red_b = drifted(solid(kRed), 14.0F);  // tight-matchable, NOT equivalent

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, red_a, 1000).outcome == CommitOutcome::filled);
    // Filled outside the duplicate radius, then pulled to 14 apart by a
    // verified update - the same live path the tie-break test uses.
    REQUIRE(router.commit(Face::L, drifted(solid(kRed), 20.0F), 2000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::L, red_b, 3000).outcome == CommitOutcome::updated);

    router.on_epoch(2);
    CHECK(router.commit(Face::U, drifted(solid(kWhite), 3.0F), 10000).outcome == CommitOutcome::deferred);
    const auto probe = router.commit(Face::R, drifted(red_a, 2.0F), 11000);
    CHECK(probe.outcome == CommitOutcome::deferred);
    CHECK(router.reorienting());
    // Nothing written anywhere.
    CHECK(centre_lab_distance(*router.slot_at(1), red_a) == 0.0F);
    CHECK(centre_lab_distance(*router.slot_at(2), red_b) == 0.0F);
}

// The former "an epoch change with at most one slot re-associates
// immediately" case lived here. Only its two re-keying SECTIONs were
// superseded: they pinned the loop-order gauge adoption that is now gone
// (see "a one-slot epoch adoption picks its roll from the stickers", below
// the oriented-roll helpers this file defines later), because one filled
// slot fixes a face but leaves four rolls, and a roll chosen by iteration
// order misfiles all nine stickers once the write path honours the gauge's
// cell component.
//
// Its third SECTION was NOT superseded and is not about roll at all - it
// is the only cover anywhere for adopt_gauge_direct's refusal branch and
// the quarantine its caller reports. It has been carried over into that
// same replacement case rather than dropped, with a retry added: its
// original assertions turn out to hold with the refusal deleted, so they
// never covered what they appeared to (see the comment there).

TEST_CASE("registration margin counts runner-ups above the commit bar", "[routing]") {
    // The header contract is "winner at most epoch_register_max_score AND
    // runner-up worse by at least the margin". A runner-up scoring just
    // above the bar (winner 4.25, runner-up 6.25, bar 6, margin 4) is
    // still a live alternative hypothesis about where the epoch's faces
    // belong - filtering it out by the bar BEFORE the margin check would
    // commit the winner as "unique" while the evidence could not actually
    // separate them.
    const RoutingConfig config;

    std::array<std::optional<CapturedFace>, 6> slots{};
    slots[static_cast<std::size_t>(Face::U)] = solid(kWhite);
    slots[static_cast<std::size_t>(Face::R)] = solid(kRed);
    // Occupied so the rotations mapping the probe here score far outside
    // the margin instead of escaping to a cheap empty target.
    slots[static_cast<std::size_t>(Face::L)] = solid(kGreen);
    slots[static_cast<std::size_t>(Face::B)] = solid(kYellow);
    // The runner-up target: same census distance from the probe as the
    // stored R slot (L1 6) but three times the centre drift, scoring
    // 6 + 2*36/12 = 12 -> candidate mean (0.5 + 12)/2 = 6.25: above the
    // 6-point bar, within 4 of the winner's 4.25.
    CapturedFace f_slot = drifted(solid(kRed), 48.0F);
    f_slot = with_sticker(f_slot, 0, kBlue);
    f_slot = with_sticker(f_slot, 1, kBlue);
    f_slot = with_sticker(f_slot, 2, kBlue);

    // The white sample tight-matches the stored U slot ONLY, pinning the
    // candidates to the four rotations fixing U; the red-ish probe (census
    // L1 6 from solid red - too drifted to pin anything itself) then maps
    // onto R (winner: 6 + 2*12/12 = 8, mean 4.25), F (runner-up), L and B
    // (both far outside the margin).
    CapturedFace probe = solid(kRed);
    probe = with_sticker(probe, 0, kGreen);
    probe = with_sticker(probe, 1, kGreen);
    probe = with_sticker(probe, 2, kGreen);
    probe = drifted(probe, 12.0F);

    std::vector<EpochFaceSample> samples;
    samples.push_back({Face::U, drifted(solid(kWhite), 3.0F)});
    samples.push_back({Face::R, probe});

    SECTION("a runner-up above the bar but inside the margin defers") {
        slots[static_cast<std::size_t>(Face::F)] = f_slot;
        const auto registration = register_epoch_gauge(samples, slots, config);
        CHECK(registration.status == EpochRegistration::Status::ambiguous);
    }

    SECTION("pushing the runner-up past the margin resolves uniquely") {
        // Three more swapped stickers (centre cell 4 untouched): census L1
        // vs the probe becomes 12, score 18, mean 9.25 - a genuine unique
        // win 5 points clear of the margin.
        f_slot = with_sticker(f_slot, 3, kBlue);
        f_slot = with_sticker(f_slot, 5, kBlue);
        f_slot = with_sticker(f_slot, 6, kBlue);
        slots[static_cast<std::size_t>(Face::F)] = f_slot;
        const auto registration = register_epoch_gauge(samples, slots, config);
        REQUIRE(registration.status == EpochRegistration::Status::resolved);
        CHECK(registration.lattice_index == kIdentityLatticeIndex);
    }
}

TEST_CASE("retakes that empty the store mid-recovery exit recovery", "[routing]") {
    // A user may retakeSlot() every filled slot inside a re-orienting
    // window. Registration needs occupied slots to score against, so
    // staying in recovery past that point would defer every commit for the
    // rest of the epoch ("re-orienting forever"); <= 1 slot is exactly
    // on_epoch's own labels-are-relative regime, so recovery must end
    // there too.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());
    // A buffered epoch face is already pending when the user starts
    // retaking - it is dropped with the recovery state and simply
    // re-commits on a later pass.
    //
    // The buffered face is a re-show of the stored white, NOT the brand-new
    // blue it used to be. A never-seen face no longer merely waits - it is
    // admitted to an empty slot on the spot (registration `insufficient` is
    // a new-face verdict, not an unresolved one), so although it does stay
    // buffered it is no longer a PENDING commit and could not stand in for
    // one here. A re-show still buffers unwritten for the right
    // reason: it tight-matches white, pinning F->U, and the four rotations
    // that satisfy that pin are indistinguishable on a solid face - a
    // genuine `ambiguous`, which is exactly the state this case needs the
    // retake to interrupt.
    const CapturedFace pending = drifted(solid(kWhite), 3.0F);
    CHECK(router.commit(Face::F, pending, 10000).outcome == CommitOutcome::deferred);

    SECTION("down to one slot: labels are relative again") {
        REQUIRE(router.clear_slot(0));  // retake white
        CHECK_FALSE(router.reorienting());
        const auto result = router.commit(Face::F, pending, 11000);
        CHECK(result.outcome == CommitOutcome::filled);
        CHECK(router.filled_count() == 2);
        // The surviving slot's content was never touched by the exit.
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
    }

    SECTION("down to zero slots: everything is fresh") {
        REQUIRE(router.clear_slot(0));
        REQUIRE(router.clear_slot(0));
        CHECK_FALSE(router.reorienting());
        const auto result = router.commit(Face::F, pending, 11000);
        CHECK(result.outcome == CommitOutcome::filled);
        CHECK(router.filled_count() == 1);
    }
}

TEST_CASE("repeated duplicate-centre quarantines escalate to force-lost", "[routing]") {
    // Identity-policy rule 3 calls the duplicate-centre quarantine slip
    // evidence with a belief penalty. A wrong gauge adopted at an epoch
    // boundary produces exactly this signature on every pass - and never a
    // verification strike, because the mis-claimed slot is EMPTY - so the
    // quarantines themselves must escalate, or the wrong gauge would only
    // ever surface as silent per-pass drops.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::R, solid(kRed), 0).outcome == CommitOutcome::filled);

    const auto first = router.commit(Face::U, solid(kWashedOrangeAsRed), 2000);
    CHECK(first.outcome == CommitOutcome::quarantined);
    CHECK(first.duplicate_centre);
    CHECK_FALSE(first.force_lost);

    const auto second = router.commit(Face::U, solid(kWashedOrangeAsRed), 4000);
    CHECK(second.outcome == CommitOutcome::quarantined);
    CHECK(second.duplicate_centre);
    CHECK(second.force_lost);
    CHECK_FALSE(second.layer_turn);
    // Slots preserved - the escalation accuses the TRACK, not the store.
    CHECK(router.filled_count() == 1);
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
}

TEST_CASE("retake clears exactly one slot by fill index", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::F, solid(kGreen), 2000).outcome == CommitOutcome::filled);

    CHECK_FALSE(router.clear_slot(3));
    REQUIRE(router.clear_slot(1));
    CHECK(router.filled_count() == 2);
    // Fill order compacts; remaining slots keep their content.
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kWhite)) == 0.0F);
    CHECK(centre_lab_distance(*router.slot_at(1), solid(kGreen)) == 0.0F);

    // The cleared face can be captured again (fresh fill at the end).
    const auto refill = router.commit(Face::R, drifted(solid(kRed), 2.0F), 4000);
    CHECK(refill.outcome == CommitOutcome::filled);
    CHECK(router.filled_count() == 3);
}

// ---------------------------------------------------------------------------
// Oriented roll registration. The structural gap these cover: census and
// centre colour are both rotation-invariant, so the four lattice elements
// fixing a face score IDENTICALLY under them. With one recognised epoch
// face that tie is unbreakable by score, and it is broken here only by the
// ordered stickers.
// ---------------------------------------------------------------------------

namespace {

// A face with nine visually distinct cells, so every roll of it is
// distinguishable - the ordinary scrambled-cube case.
[[nodiscard]] CapturedFace patterned() {
    CapturedFace face = solid(kRed);
    face.lab[0] = kBlue;
    face.lab[1] = kBlue;
    face.lab[2] = kGreen;
    face.lab[3] = kWhite;
    face.lab[5] = kGreen;
    face.lab[6] = kYellow;
    face.lab[7] = kWhite;
    face.lab[8] = kYellow;
    return face;
}

// The same physical face as the store would hold it after a whole-cube
// rotation by `lattice_index`: cell k of `source` on `source_face` becomes
// cell kBodyRelabel[...][k] of the face it rotates onto.
[[nodiscard]] CapturedFace relabelled(const CapturedFace& source, Face source_face, int lattice_index) {
    const auto& map = kBodyRelabel[static_cast<std::size_t>(lattice_index)][static_cast<std::size_t>(source_face)];
    CapturedFace out;
    for (std::size_t k = 0; k < source.lab.size(); ++k) {
        out.lab[static_cast<std::size_t>(map.cell[k])] = source.lab[k];
    }
    return out;
}

// Near-boundary tiles, for building a face whose census has moved by a
// known amount at a SMALL Lab cost - the combination the roll gates care
// about, since the roll ranks Lab and the score ranks census. Each sits
// just the wrong side of a classify_lab band edge from a `patterned()`
// colour.
const cv::Vec3f kWhiteJustRed{96.0F, 14.0F, 8.0F};     // chroma 16.1 -> red, 13.4 Lab from kWhite
const cv::Vec3f kYellowJustOrange{88.0F, 23.0F, 80.0F};  // hue 74 -> orange, 18.0 Lab from kYellow

}  // namespace

TEST_CASE("a single epoch face registers its roll against two filled slots", "[routing]") {
    // THE live5 case, and the reason a six-face scan could not complete.
    // The store holds two faces that are NOT opposites, and the epoch has
    // recognised exactly one of them. Exactly four permutations send that
    // epoch face to that slot; they score identically on every
    // rotation-invariant term, so the runner-up margin can never separate
    // them. Four times in five the second slot lands inside one of their
    // 4-cycles, which used to make the outcome `ambiguous` with
    // certainty - registration deferred forever and coverage stalled at
    // two faces.
    const RoutingConfig config;
    const CapturedFace pattern = patterned();

    // A true gauge that is NOT the identity, so a table that quietly
    // ignored the roll could not pass by accident.
    constexpr int kTrueGauge = 4 * 2 + 1;  // quarter turn about the F axis
    const Face store_face = kFacePermutation[kTrueGauge][static_cast<std::size_t>(Face::F)];
    REQUIRE(store_face == Face::F);

    std::array<std::optional<CapturedFace>, 6> slots{};
    slots[static_cast<std::size_t>(store_face)] = relabelled(pattern, Face::F, kTrueGauge);
    // The second slot: a different physical face, NOT the opposite of the
    // first (opposite is the one-in-five case that already resolved).
    slots[static_cast<std::size_t>(Face::U)] = solid(kOrange);
    REQUIRE(Face::U != Face::B);  // B is F's opposite

    std::vector<EpochFaceSample> samples;
    samples.push_back({Face::F, pattern});

    SECTION("the correct roll is chosen, not merely the correct face") {
        const auto registration = register_epoch_gauge(samples, slots, config);
        REQUIRE(registration.status == EpochRegistration::Status::resolved);
        CHECK(registration.basis == EpochRegistration::Basis::oriented);
        CHECK(registration.lattice_index == kTrueGauge);
        // The tie really was the structural four-fold one.
        CHECK(registration.tied == 4);
        CHECK(registration.best_oriented_lab < registration.runner_up_oriented_lab);
    }

    SECTION("every wrong roll of the right face is rejected") {
        // Each of the other three rotations fixing F maps the epoch face
        // onto the same slot with the same census and centre - only the
        // sticker order tells them apart, so registration must land on
        // exactly one of the four.
        for (int roll = 0; roll < 4; ++roll) {
            const int gauge = 4 * 2 + roll;
            std::array<std::optional<CapturedFace>, 6> rolled{};
            rolled[static_cast<std::size_t>(kFacePermutation[static_cast<std::size_t>(gauge)]
                                                           [static_cast<std::size_t>(Face::F)])] =
                relabelled(pattern, Face::F, gauge);
            rolled[static_cast<std::size_t>(Face::U)] = solid(kOrange);
            const auto registration = register_epoch_gauge(samples, rolled, config);
            REQUIRE(registration.status == EpochRegistration::Status::resolved);
            CHECK(registration.lattice_index == gauge);
        }
    }

    SECTION("a rotationally symmetric face stays ambiguous") {
        // A solid face reads the same under all four rolls, so the
        // oriented margin is exactly zero and the tie-break must decline
        // rather than invent a roll. Content equivalence then also
        // declines (the second slot sits inside a 4-cycle whose other
        // targets are empty), leaving the honest refusal.
        std::array<std::optional<CapturedFace>, 6> symmetric{};
        symmetric[static_cast<std::size_t>(Face::F)] = solid(kRed);
        symmetric[static_cast<std::size_t>(Face::U)] = solid(kOrange);
        std::vector<EpochFaceSample> solid_sample;
        solid_sample.push_back({Face::F, solid(kRed)});
        const auto registration = register_epoch_gauge(solid_sample, symmetric, config);
        CHECK(registration.status == EpochRegistration::Status::ambiguous);
    }

    SECTION("a face matching no stored slot is refused, however its rolls rank") {
        // The oriented tie-break may only adjudicate a tie the score
        // already accepted; it must never rescue a hypothesis that failed
        // the commit bar. A wholly unseen face is the `insufficient`
        // deferral that keeps new content out of an occupied store.
        std::array<std::optional<CapturedFace>, 6> unseen{};
        unseen[static_cast<std::size_t>(Face::F)] = solid(kGreen);
        unseen[static_cast<std::size_t>(Face::U)] = solid(kOrange);
        const auto registration = register_epoch_gauge(samples, unseen, config);
        CHECK(registration.status != EpochRegistration::Status::resolved);
    }
}

TEST_CASE("the roll tie-break cannot promote a candidate the score rejected", "[routing]") {
    // The reachable case the earlier bar check missed. `tied` membership
    // only required being within epoch_register_margin (4) of the best, so
    // a tied member could score up to 10 against a 6 bar - and the roll
    // sort, which ranks the tied set purely on ordered stickers, could
    // return that member. The bar was tested against the best candidate
    // only, which stopped being the returned one.
    //
    // Store: the true face (score at the bar, poor roll) on one slot, and
    // a DIFFERENT physical face whose ring aliases the epoch face exactly
    // (roll 0) but whose centre is a whole colour class away, on another.
    // Registration must never adopt the aliasing face's gauge.
    const RoutingConfig config;
    const CapturedFace pattern = patterned();

    // Some lattice element sending the epoch's F somewhere other than F -
    // the wrong-face hypothesis whose gauge must not be adopted.
    int wrong_gauge = -1;
    for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
        if (kFacePermutation[static_cast<std::size_t>(g)][static_cast<std::size_t>(Face::F)] == Face::U) {
            wrong_gauge = g;
            break;
        }
    }
    REQUIRE(wrong_gauge >= 0);

    std::vector<EpochFaceSample> samples;
    samples.push_back({Face::F, pattern});

    // Slot F in both sections: the true face under the identity, drifted
    // by three sticker reclassifications ONTO ONE NEW COLOUR so the census
    // moves by the full 6 rather than partly cancelling. That is
    // deliberate twice over - it puts the correct hypothesis exactly AT
    // the commit bar (census 6, centre unmoved), and it takes the pair
    // past reassoc_census (4) so slot F does not tight-match the epoch
    // face. A lone tight match would PIN F->F and drop the aliasing
    // hypothesis before scoring, which is precisely how the previous
    // version of this test managed to pass with the gates deleted.
    CapturedFace true_face = pattern;
    true_face.lab[0] = kOrange;
    true_face.lab[2] = kOrange;
    true_face.lab[6] = kOrange;

    SECTION("an over-bar tied candidate with a plausible roll is refused") {
        // The score-bar-on-the-winner gate on its own. Slot U holds a
        // DIFFERENT physical face whose ring aliases the epoch face
        // closely enough to win the roll sort outright, and whose centre
        // agrees in class and sits well inside reassoc_centre_lab - so the
        // oriented bar, the oriented margin and the centre gate all pass.
        // Its census has moved by 6 though, which with the centre offset
        // puts it ABOVE epoch_register_max_score while still inside
        // epoch_register_margin of the winner, i.e. squarely in the tied
        // set. Only the bar on by_roll.front() refuses it.
        CapturedFace aliasing_source = pattern;
        aliasing_source.lab[3] = kWhiteJustRed;
        aliasing_source.lab[7] = kWhiteJustRed;
        aliasing_source.lab[6] = kYellowJustOrange;
        CapturedFace aliasing = relabelled(aliasing_source, Face::F, wrong_gauge);
        // Same class, a sixth of the tight radius away: enough centre Lab
        // to lift the pair score past the bar, nowhere near enough to
        // trouble the centre gate.
        aliasing.lab[cube::model::center_facelet_index] =
            pattern.lab[cube::model::center_facelet_index] + cv::Vec3f{6.0F, 0.0F, 0.0F};

        std::array<std::optional<CapturedFace>, 6> slots{};
        slots[static_cast<std::size_t>(Face::U)] = aliasing;
        slots[static_cast<std::size_t>(Face::F)] = true_face;

        const auto registration = register_epoch_gauge(samples, slots, config);

        // The configuration really is the one described: eight surviving
        // hypotheses (four fixing F, four sending F to U), all tied.
        REQUIRE(registration.candidates == 8);
        REQUIRE(registration.tied == 8);
        // A winner at the bar and a tied member above it - the shape the
        // bar-on-the-best check could not see. (`runner_up_score` cannot
        // show this: the four rolls fixing F all score identically, so
        // candidates[1] is one of THEM, not the aliasing hypothesis. With
        // a single epoch face the aliasing candidate's mean score is just
        // its one pair score.)
        REQUIRE(registration.best_score <= config.epoch_register_max_score);
        const float aliasing_score = registration_pair_score(pattern, aliasing, config);
        REQUIRE(aliasing_score > config.epoch_register_max_score);
        REQUIRE(aliasing_score - registration.best_score < config.epoch_register_margin);
        // ...and the over-bar member is the one the roll sort returns.
        REQUIRE(registration.best_oriented_lab ==
                oriented_cell_lab_distance(pattern, aliasing, Face::F, wrong_gauge));
        // Every OTHER oriented gate passes, so nothing but the score bar
        // is left to refuse.
        REQUIRE(registration.best_oriented_lab <= config.epoch_register_oriented_max_lab);
        REQUIRE(registration.runner_up_oriented_lab - registration.best_oriented_lab >=
                config.epoch_register_oriented_margin_lab);

        // The outcome the gate changes: without it this resolves onto the
        // aliasing slot's gauge.
        CHECK(registration.status != EpochRegistration::Status::resolved);
        CHECK(registration.lattice_index != wrong_gauge);
    }

    SECTION("a bar-passing candidate whose centre is a class away is refused") {
        // The centre-agreement gate on its own. Slot U holds the epoch
        // face's ring relabelled under wrong_gauge, so its oriented
        // distance is exactly zero - a perfect roll. Its centre is a
        // WHOLE COLOUR CLASS away (hue 45, just over the red/orange
        // boundary) yet only 18 Lab from the epoch centre, so the pair
        // costs 2 census points plus 3 score points of centre and passes
        // the commit bar comfortably. The class half of the centre gate is
        // the only thing standing between it and a wrong-slot resolution.
        CapturedFace aliasing = relabelled(pattern, Face::F, wrong_gauge);
        aliasing.lab[cube::model::center_facelet_index] = cv::Vec3f{45.0F, 50.0F, 50.0F};
        REQUIRE(classify_lab(aliasing.lab[cube::model::center_facelet_index]).color == FacetColor::orange);
        REQUIRE(centre_lab_distance(pattern, aliasing) < config.same_face_centre_lab);

        std::array<std::optional<CapturedFace>, 6> slots{};
        slots[static_cast<std::size_t>(Face::U)] = aliasing;
        slots[static_cast<std::size_t>(Face::F)] = true_face;

        const auto registration = register_epoch_gauge(samples, slots, config);

        REQUIRE(registration.candidates == 8);
        REQUIRE(registration.tied == 8);
        // The aliasing hypothesis is the best-scoring one AND is under the
        // bar, so the score gate cannot be what refuses it.
        REQUIRE(registration.best_score <= config.epoch_register_max_score);
        // Its roll is perfect and clears the runner-up by more than the
        // margin, so neither oriented gate can be what refuses it either.
        REQUIRE(registration.best_oriented_lab == 0.0F);
        REQUIRE(registration.runner_up_oriented_lab >= config.epoch_register_oriented_margin_lab);

        CHECK(registration.status != EpochRegistration::Status::resolved);
        CHECK(registration.lattice_index != wrong_gauge);
    }
}

TEST_CASE("oriented cell distance reads zero only for the true relabel", "[routing]") {
    const CapturedFace pattern = patterned();
    for (int gauge = 0; gauge < static_cast<int>(kLatticeSize); ++gauge) {
        const CapturedFace stored = relabelled(pattern, Face::F, gauge);
        // Under its own gauge every sticker lines up exactly.
        CHECK(oriented_cell_lab_distance(pattern, stored, Face::F, gauge) == 0.0F);
    }
    // ...and under a different roll of the same face it does not. The
    // centre is excluded, so this is purely the eight ring stickers.
    const CapturedFace stored = relabelled(pattern, Face::F, 4 * 2 + 1);
    CHECK(oriented_cell_lab_distance(pattern, stored, Face::F, 4 * 2 + 2) > 0.0F);
}

TEST_CASE("a tracker reset re-opens registration on a two-face store", "[routing]") {
    // The retake defect. ScanSession::retakeFace() re-inits the tracker -
    // restarting its epoch counter at zero - but deliberately KEEPS the
    // slot store, because a retake re-reads one face rather than discarding
    // the scan. The router must be told, and told SEPARATELY from
    // on_epoch(): the restarted counter can hand back a number the router
    // has already seen, and on_epoch() early-returns on an unchanged
    // number, leaving gauge_ describing the dead track's body frame while
    // every later commit is routed under it.
    //
    // What this asserts is the observable consequence of the notification
    // the buggy path omitted entirely: with two faces stored, a new locked
    // segment must enter recovery (commits buffer until the gauge
    // re-registers against stored content) rather than carrying on as
    // though the old frame still applied.
    const RoutingConfig config;
    CaptureRouter router{config};

    router.on_epoch(7);
    REQUIRE(router.commit(Face::F, solid(kRed), 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::U, solid(kGreen), 2000).outcome == CommitOutcome::filled);
    REQUIRE(router.filled_count() == 2);
    REQUIRE_FALSE(router.reorienting());

    // The tracker was reset underneath us. Without this notification the
    // router keeps a live gauge for a track that no longer exists; the
    // subsequent on_epoch() cannot rescue it, because the restarted counter
    // may reproduce an epoch number already seen.
    router.on_tracker_reset();
    CHECK(router.reorienting());
    // Both captures survive - a retake must not discard the scan.
    CHECK(router.filled_count() == 2);

    // And the number really is forgotten: re-registration still happens
    // even when the fresh anchor reproduces the pre-reset epoch number,
    // which is precisely the collision on_epoch()'s early return could not
    // distinguish from "nothing changed".
    router.on_epoch(7);
    CHECK(router.reorienting());
}

// ---------------------------------------------------------------------------
// The capture ROUTING repair: what gets written, and under whose frame.
//
// Everything above this line tests where a capture is ROUTED. These test
// what actually lands in the slot - the half of the gauge (kBodyRelabel,
// the cell relabelling) that the write path used to discard, and the
// same-face predicate that used to be unable to separate the closest
// legitimate colour pair on a cube.
// ---------------------------------------------------------------------------

TEST_CASE("relabel_capture is the write-path inverse of the oriented comparison", "[routing]") {
    // Definitional, not a regression: it pins that the map the router
    // WRITES through and the map it SCORES through are the same map, so a
    // slot written under a gauge reads back as a perfect oriented match
    // under that same gauge. If those two ever disagree, registration
    // scores every subsequent hypothesis against bytes it did not expect.
    const CapturedFace pattern = patterned();
    for (int gauge = 0; gauge < static_cast<int>(kLatticeSize); ++gauge) {
        for (std::size_t f = 0; f < 6; ++f) {
            const auto face = static_cast<Face>(f);
            const CapturedFace stored = relabel_capture(pattern, face, gauge);
            // Agrees with this file's own independently written relabeller.
            const CapturedFace reference = relabelled(pattern, face, gauge);
            for (std::size_t cell = 0; cell < stored.lab.size(); ++cell) {
                REQUIRE(cv::norm(stored.lab[cell] - reference.lab[cell]) == 0.0);
            }
            // And is exactly what the oriented comparison expects to find.
            REQUIRE(oriented_cell_lab_distance(pattern, stored, face, gauge) == 0.0F);
        }
    }
    // The centre is rotation-invariant: it must survive every relabelling
    // in place, or centre_lab_distance would be comparing the wrong cells.
    for (int gauge = 0; gauge < static_cast<int>(kLatticeSize); ++gauge) {
        const CapturedFace stored = relabel_capture(pattern, Face::F, gauge);
        REQUIRE(cv::norm(stored.lab[cube::model::center_facelet_index] -
                         pattern.lab[cube::model::center_facelet_index]) == 0.0);
    }
}

TEST_CASE("a recovery flush stores its captures in the store's frame", "[routing]") {
    // THE D3 regression, at router level. All three corpus corruptions
    // (live5 F pass 122 at a 90-degree roll, live5 L pass 250 and live1 D
    // pass 236 at mirrored maps) came through this path: registration
    // resolved the gauge correctly - its winning oriented reading matched
    // the independently measured best dihedral fit to within 0.1 Lab every
    // time - and commit_direct then stored the epoch-frame array verbatim,
    // so the slot label was right and its nine cells were a rotated lie.
    //
    // Not merely an assembly bug: the corrupted slot then poisons
    // oriented_cell_lab_distance for every later hypothesis, which is what
    // drove live4 and live5 into the 30+ Lab readings that failed the
    // 20-Lab bar and produced the corpus's dominant refusal (deferral).
    const CapturedFace pattern = patterned();
    constexpr int kTrueGauge = 4 * 2 + 1;  // quarter turn about the F axis
    REQUIRE(kFacePermutation[kTrueGauge][static_cast<std::size_t>(Face::F)] == Face::F);

    CaptureRouter router;
    router.on_epoch(1);
    // Two slots, so the epoch change enters recovery proper and the write
    // under test is the registration flush.
    REQUIRE(router.commit(Face::F, relabelled(pattern, Face::F, kTrueGauge), 0).outcome ==
            CommitOutcome::filled);
    REQUIRE(router.commit(Face::U, relabelled(solid(kOrange), Face::U, kTrueGauge), 1000).outcome ==
            CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());
    const auto result = router.commit(Face::F, pattern, 10000);
    REQUIRE(result.registration.has_value());
    REQUIRE(result.registration->status == EpochRegistration::Status::resolved);
    CHECK(result.registration->lattice_index == kTrueGauge);
    CHECK(result.outcome == CommitOutcome::updated);

    const CapturedFace expected = relabelled(pattern, Face::F, kTrueGauge);
    // Stated as the failure it replaces: under this gauge the epoch-frame
    // array is NOT the store-frame array, so a verbatim write is
    // detectable and this test cannot pass both ways.
    bool differs = false;
    for (std::size_t cell = 0; cell < expected.lab.size(); ++cell) {
        differs = differs || cv::norm(expected.lab[cell] - pattern.lab[cell]) > 0.0;
    }
    REQUIRE(differs);

    const CapturedFace* content = router.slot_at(0);
    REQUIRE(content != nullptr);
    for (std::size_t cell = 0; cell < expected.lab.size(); ++cell) {
        CHECK(cv::norm(content->lab[cell] - expected.lab[cell]) == 0.0);
    }
}

TEST_CASE("a one-slot epoch adoption picks its roll from the stickers", "[routing]") {
    // D1. With at most one slot filled the epoch adopts a gauge on the spot
    // rather than buffering, and the slot LABEL really is relative there -
    // cross-session evidence confirms sessions legitimately disagree on
    // names. The ROLL is not relative: four lattice elements send a given
    // epoch face to a given store face, they are indistinguishable to every
    // rotation-invariant signal, and the old adopt_gauge_direct took
    // whichever came first in index order.
    //
    // That guess was invisible while the write path ignored the gauge's
    // cell component - which is why not one of the three roll corruptions
    // measured on the corpus came through here. It stops being invisible
    // the moment commit_direct relabels, so this fix and the write-path fix
    // are one change: live3 alone ran 21 epochs with exactly one filled
    // slot, and every re-show in that regime would have been filed under a
    // guessed rotation.
    const CapturedFace pattern = patterned();
    // Four elements fix F: 8, 9, 10, 11. Deliberately NOT the lowest, so
    // loop-order adoption lands somewhere measurably different.
    constexpr int kTrueGauge = 4 * 2 + 1;
    REQUIRE(kFacePermutation[kTrueGauge][static_cast<std::size_t>(Face::F)] == Face::F);
    REQUIRE(kIdentityLatticeIndex == 8);
    const CapturedFace stored = relabelled(pattern, Face::F, kTrueGauge);

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, stored, 0).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    // One slot: direct adoption, no buffering - the coverage-preserving
    // regime, unchanged.
    REQUIRE_FALSE(router.reorienting());

    SECTION("a re-show is filed under the roll the stickers support") {
        const auto result = router.commit(Face::F, pattern, 2000);
        CHECK(result.outcome == CommitOutcome::updated);
        REQUIRE(router.filled_count() == 1);

        // The discriminator: under kTrueGauge the store-frame array is NOT
        // the epoch-frame array, so a roll chosen by loop order (which
        // would have taken the identity, index 8) writes detectably
        // different bytes. This test cannot pass both ways.
        bool differs = false;
        for (std::size_t cell = 0; cell < stored.lab.size(); ++cell) {
            differs = differs || cv::norm(stored.lab[cell] - pattern.lab[cell]) > 0.0;
        }
        REQUIRE(differs);

        const CapturedFace* content = router.slot_at(0);
        REQUIRE(content != nullptr);
        for (std::size_t cell = 0; cell < stored.lab.size(); ++cell) {
            CHECK(cv::norm(content->lab[cell] - stored.lab[cell]) == 0.0);
        }
    }

    SECTION("every roll of the stored face is recoverable, not just one") {
        // Sweep all four elements fixing F: the choice must track the
        // evidence, not a fixed index. A loop-order adoption passes exactly
        // one of these four by luck.
        for (int roll = 0; roll < 4; ++roll) {
            const int gauge = 4 * 2 + roll;
            const CapturedFace rolled = relabelled(pattern, Face::F, gauge);
            CaptureRouter fresh;
            fresh.on_epoch(1);
            REQUIRE(fresh.commit(Face::F, rolled, 0).outcome == CommitOutcome::filled);
            fresh.on_epoch(2);
            REQUIRE(fresh.commit(Face::F, pattern, 2000).outcome == CommitOutcome::updated);
            const CapturedFace* content = fresh.slot_at(0);
            REQUIRE(content != nullptr);
            for (std::size_t cell = 0; cell < rolled.lab.size(); ++cell) {
                REQUIRE(cv::norm(content->lab[cell] - rolled.lab[cell]) == 0.0);
            }
        }
    }

    SECTION("a clearly different face still files beside the stored slot") {
        // Coverage guard. Nothing observable relates an unrecognised face
        // to the one stored slot, so no evidence exists for EITHER half of
        // the gauge here - and requiring evidence anyway was measured to
        // cost 4 of the corpus's 11 captured slots (live1 2->1, live2 3->1,
        // live4 2->1) while the never-overwrite invariant, which is what
        // this repair is about, is not even at stake on an empty target.
        // So the newcomer still lands beside the stored slot.
        const auto result = router.commit(Face::U, solid(kBlue), 2000);
        CHECK(result.outcome == CommitOutcome::filled);
        CHECK(router.filled_count() == 2);
        // And the stored slot is untouched, whichever label the newcomer got.
        const CapturedFace* content = router.slot_at(0);
        REQUIRE(content != nullptr);
        for (std::size_t cell = 0; cell < stored.lab.size(); ++cell) {
            CHECK(cv::norm(content->lab[cell] - stored.lab[cell]) == 0.0);
        }
    }

    SECTION("a near-duplicate centre that fails the tight match quarantines") {
        // Restored from the retired "an epoch change with at most one slot
        // re-associates immediately" case: this is the only place that
        // reaches adopt_gauge_direct's refusal branch and its caller's
        // quarantine/duplicate_centre reporting. It survives the D1 rewrite
        // untouched because the refusal fires BEFORE any roll is ranked -
        // the fixture below only needs the store's CENTRE, and the centre
        // cell is rotation-invariant, so `patterned()` under kTrueGauge
        // presents the same red centre the original solid store did.
        //
        // Centre within the duplicate radius of the stored red, but three
        // stickers read green (census past the re-association drift): could
        // be the stored face badly lit, could be a different face aliasing
        // it - the router adopts nothing and writes nothing.
        CapturedFace suspect = solid(kWashedOrangeAsRed);
        suspect = with_sticker(suspect, 0, kGreen);
        suspect = with_sticker(suspect, 1, kGreen);
        suspect = with_sticker(suspect, 2, kGreen);
        const auto result = router.commit(Face::U, suspect, 2000);
        CHECK(result.outcome == CommitOutcome::quarantined);
        CHECK(result.duplicate_centre);
        CHECK(router.filled_count() == 1);
        const CapturedFace* content = router.slot_at(0);
        REQUIRE(content != nullptr);
        for (std::size_t cell = 0; cell < stored.lab.size(); ++cell) {
            CHECK(cv::norm(content->lab[cell] - stored.lab[cell]) == 0.0);
        }

        // The assertion that actually pins the refusal branch, and the
        // reason the outcome checks above cannot do it alone: deleting the
        // refusal lets the !matches path adopt a NON-COLLIDING gauge, so
        // the capture reaches commit_direct against an EMPTY target, where
        // the second duplicate-centre scan quarantines it with the same
        // outcome and the same flag. The two are distinguishable only on
        // retry. Refusing leaves gauge_known_ false and touches no counter
        // (adopt has no strike state by design - see capture_routing.cpp,
        // "each retry re-tests genuinely fresh evidence rather than
        // re-accusing a fixed claimed face"), so a second identical pass
        // must quarantine again WITHOUT escalating. Adopting instead pins
        // the gauge and routes the retry through commit_direct's counter,
        // which reaches conflict_strikes (2) on this very pass and
        // force-loses a track on evidence the router explicitly declined
        // to trust.
        const auto retry = router.commit(Face::U, suspect, 4000);
        CHECK(retry.outcome == CommitOutcome::quarantined);
        CHECK(retry.duplicate_centre);
        CHECK_FALSE(retry.force_lost);
        CHECK(router.filled_count() == 1);
    }
}

TEST_CASE("a different physical face cannot verify into an occupied slot", "[routing]") {
    // The never-overwrite invariant, and the user's original complaint.
    // Measured breach: live3 slot F, pass 104 -> 112, BOTH inside epoch 6
    // (no epoch change, no registration ran), centre class yellow -> red at
    // 28.1 Lab and census L1 2, outcome `updated`. It was admitted because
    // the verify radius was 30.0 while the CLOSEST LEGITIMATE centre pair
    // on a cube (red/orange) is ~27 Lab apart - a radius that cannot
    // separate red from orange is not a same-face test at all. All eight
    // dihedral maps of the old content sat flat at 35.4-38.6 mean Lab, so
    // no roll and no mirror explained the new stickers: it was another
    // face, and it destroyed live3's only captured slot.
    //
    // Reconstructed at the same numbers: eight shared white stickers
    // (census L1 2, well inside verify_census_drift) and a centre 27.9 Lab
    // away in a DIFFERENT colour class.
    const cv::Vec3f kOrangeNearRed{60.6F, 44.4F, 52.2F};  // hue ~50 -> orange
    REQUIRE(cv::norm(kOrangeNearRed - kRed) > 25.0);
    REQUIRE(cv::norm(kOrangeNearRed - kRed) < 30.0);
    REQUIRE(classify_lab(kRed).color != classify_lab(kOrangeNearRed).color);

    CapturedFace stored = solid(kWhite);
    stored.lab[cube::model::center_facelet_index] = kRed;
    CapturedFace intruder = solid(kWhite);
    intruder.lab[cube::model::center_facelet_index] = kOrangeNearRed;
    REQUIRE(census_l1_distance(color_census(stored), color_census(intruder)) == 2);

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, stored, 0).outcome == CommitOutcome::filled);

    // Same epoch, same tracked face, past the recommit holdoff: identity
    // routes it straight onto the occupied slot, so verification is the
    // ONLY thing standing between it and the stored capture.
    const auto result = router.commit(Face::F, intruder, 5000);
    CHECK(result.outcome == CommitOutcome::quarantined);
    CHECK(router.filled_count() == 1);
    const CapturedFace* content = router.slot_at(0);
    REQUIRE(content != nullptr);
    CHECK(cv::norm(content->lab[cube::model::center_facelet_index] - kRed) == 0.0);
}

TEST_CASE("honest same-face drift still verifies after the radius tightened", "[routing]") {
    // The other half of the D2 fix, and why the radius was not simply
    // lowered below the breach. The corpus's largest HONEST same-face
    // centre drift is 22.7 Lab (live3 slot F, pass 42 -> 104, yellow ->
    // yellow); a radius under that refuses a legitimate update, and a
    // refused update is not free - it takes a strike, and conflict_strikes
    // = 2 escalates to force_lost, which costs coverage. Centre CLASS
    // equality is what refuses the breach; the radius only has to clear
    // real drift while staying inside the palette.
    CapturedFace stored = solid(kWhite);
    stored.lab[cube::model::center_facelet_index] = kYellow;
    CapturedFace same_face_drifted = solid(kWhite);
    same_face_drifted.lab[cube::model::center_facelet_index] = kYellow + cv::Vec3f{-16.0F, 8.0F, -14.0F};
    REQUIRE(cv::norm(same_face_drifted.lab[cube::model::center_facelet_index] - kYellow) > 22.0);
    REQUIRE(classify_lab(same_face_drifted.lab[cube::model::center_facelet_index]).color ==
            classify_lab(kYellow).color);

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, stored, 0).outcome == CommitOutcome::filled);
    CHECK(router.commit(Face::F, same_face_drifted, 5000).outcome == CommitOutcome::updated);
}

// ---------------------------------------------------------------------------
// Empty-slot admission during recovery: registration `insufficient` is a
// NEW-FACE verdict, not an unresolved one.
//
// The measured defect these cover. Across four deliberate live scans of one
// scramble (runA..runD, 8 face presentations covering all 6 faces), coverage
// ceilinged at exactly two filled slots in every clip. begin_epoch_scope
// enters recovery the moment the store holds two, all four runs reached two
// within ~50 passes, and from there nothing could be written unless
// register_epoch_gauge returned `resolved` - which it structurally cannot for
// a face the scan has never captured, because it only scores permutations
// mapping a visible epoch face onto an ALREADY-FILLED slot (pairs > 0). Every
// such candidate scores the newcomer against the wrong stored face: measured
// 9.8-16.1 against the 6.0 commit bar, `insufficient` 40 times in 79
// registration attempts, 51 deferrals against 9 fills over the four runs, and
// registration resolving only 3 times - always on a re-show, always in the
// last 15% of a clip. runD is the sharpest: four geometry-confidence 1.0
// nine-of-nine reads at passes 273-286, all refused at 10.55-12.62.
//
// What the fix does NOT change, asserted throughout below: the newcomer is
// only ever routed to an EMPTY key, the duplicate-centre scan still refuses
// it if its centre sits on a captured face's, an occupied slot still only
// changes through the verified in-place update, and `ambiguous` still defers.
// ---------------------------------------------------------------------------

TEST_CASE("a never-seen face fills an empty slot mid-recovery", "[routing]") {
    // (a) The exact live scenario: two slots filled, a new epoch, and a face
    // that resembles nothing stored. PRE-FIX this deferred forever - it is
    // the assertion that separates this test from the old behaviour, and it
    // fails on the old code at the very first CHECK.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kGreen), 1000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());

    // Tracked as body U this epoch - the label the store's white already
    // holds, so the identity gauge is NOT available and the router has to
    // file the newcomer onto some other, empty key.
    const CapturedFace newcomer = patterned();
    REQUIRE(kFacePermutation[kIdentityLatticeIndex][static_cast<std::size_t>(Face::U)] == Face::U);

    const auto result = router.commit(Face::U, newcomer, 10000);
    CHECK(result.outcome == CommitOutcome::filled);
    REQUIRE(result.registration.has_value());
    // The status the fix reinterprets: nothing tight-matched anything.
    CHECK(result.registration->status == EpochRegistration::Status::insufficient);
    // The epoch stays in recovery: see "a brand-new face is admitted straight
    // into an empty slot" above, and the runD overwrite the non-promotion
    // exists to prevent.
    CHECK(router.reorienting());
    REQUIRE(router.filled_count() == 3);
    REQUIRE(result.confirmed_slots.size() == 1);
    CHECK(result.confirmed_slots.front() == 2);

    // The invariant, stated as bytes: the two captured slots are untouched.
    for (std::size_t cell = 0; cell < 9; ++cell) {
        CHECK(cv::norm(router.slot_at(0)->lab[cell] - solid(kWhite).lab[cell]) == 0.0);
        CHECK(cv::norm(router.slot_at(1)->lab[cell] - solid(kGreen).lab[cell]) == 0.0);
    }
    // ...and it landed on a key that was EMPTY, not on U or R.
    const auto store_face = router.face_at(2);
    REQUIRE(store_face.has_value());
    CHECK(*store_face != Face::U);
    CHECK(*store_face != Face::R);

    // What LANDED is the capture relabelled into the store's frame under a
    // gauge that really does send body U to that key - the same write path
    // the resolved flush uses, not a verbatim copy filed under a rotated
    // label (see relabel_capture's three measured corpus corruptions).
    const CapturedFace* content = router.slot_at(2);
    REQUIRE(content != nullptr);
    bool consistent = false;
    for (int g = 0; g < static_cast<int>(kLatticeSize); ++g) {
        if (kFacePermutation[static_cast<std::size_t>(g)][static_cast<std::size_t>(Face::U)] != *store_face) {
            continue;
        }
        const CapturedFace expected = relabelled(newcomer, Face::U, g);
        bool same = true;
        for (std::size_t cell = 0; cell < expected.lab.size(); ++cell) {
            same = same && cv::norm(content->lab[cell] - expected.lab[cell]) == 0.0;
        }
        consistent = consistent || same;
    }
    CHECK(consistent);
    // Both halves of that: no sticker invented or lost (census), and the
    // rotation-invariant centre survives in place.
    CHECK(census_l1_distance(color_census(*content), color_census(newcomer)) == 0);
    CHECK(cv::norm(content->lab[cube::model::center_facelet_index] -
                   newcomer.lab[cube::model::center_facelet_index]) == 0.0);
}

TEST_CASE("a mid-recovery newcomer only ever lands on an empty key", "[routing]") {
    // (b) The never-overwrite invariant on the new path, swept rather than
    // sampled. Five slots are captured, leaving exactly ONE empty key; the
    // sixth face then arrives tracked under every possible body label in
    // turn. Whatever it is called, it must land in that one empty key - the
    // "identity decides WHERE" rule cannot be allowed to point a never-seen
    // face at an occupied slot just because the epoch's labels happen to
    // line up that way.
    //
    // Fails PRE-FIX on the outcome (every iteration deferred). Fails against
    // the obvious wrong implementation too - one that reused
    // adopt_gauge_direct's old "not fill_order_.front()" rule, which with
    // five filled slots leaves four occupied targets still available.
    const std::array<std::pair<Face, cv::Vec3f>, 5> prefill{{{Face::U, kWhite},
                                                             {Face::R, kYellow},
                                                             {Face::F, kRed},
                                                             {Face::D, kGreen},
                                                             {Face::L, kOrange}}};
    for (std::size_t label = 0; label < 6; ++label) {
        const auto epoch_face = static_cast<Face>(label);
        CaptureRouter router;
        router.on_epoch(1);
        std::int64_t t = 0;
        for (const auto& entry : prefill) {
            t += 1000;
            REQUIRE(router.commit(entry.first, solid(entry.second), t).outcome == CommitOutcome::filled);
        }
        REQUIRE(router.filled_count() == 5);
        REQUIRE(router.face_at(0) == Face::U);

        router.on_epoch(2);
        REQUIRE(router.reorienting());
        const auto result = router.commit(epoch_face, solid(kBlue), 20000);
        REQUIRE(result.registration.has_value());
        CHECK(result.registration->status == EpochRegistration::Status::insufficient);
        CHECK(result.outcome == CommitOutcome::filled);
        REQUIRE(router.filled_count() == 6);
        // B is the only key left, whatever the newcomer was tracked as.
        CHECK(router.face_at(5) == Face::B);
        // Every pre-existing capture still holds its own colour.
        for (std::size_t i = 0; i < prefill.size(); ++i) {
            REQUIRE(router.slot_at(static_cast<int>(i)) != nullptr);
            CHECK(centre_lab_distance(*router.slot_at(static_cast<int>(i)), solid(prefill[i].second)) == 0.0F);
        }
    }
}

TEST_CASE("admission does not open a back door onto an occupied slot", "[routing]") {
    // (b), continued: a newcomer's slot must be no easier to overwrite than
    // any other, and the route to it is the verified in-place update or
    // nothing. The epoch is walked to a RESOLVED gauge first, because that
    // is now the only way out of recovery - admitting a newcomer fills a key
    // and deliberately leaves the epoch re-orienting (its gauge was a guess
    // and does not get to govern anything else).
    const CapturedFace pattern = patterned();
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, pattern, 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kGreen), 1000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());
    REQUIRE(router.commit(Face::F, solid(kBlue), 10000).outcome == CommitOutcome::filled);
    REQUIRE(router.filled_count() == 3);
    // Fails PRE-FIX, where the guessed gauge was promoted on the spot.
    REQUIRE(router.reorienting());

    // A re-show of the face captured at U pins the epoch against content the
    // guess never touched, and the roll tie-break settles the rotation. Only
    // now is the router routing directly.
    const auto resolved = router.commit(Face::U, pattern, 11000);
    REQUIRE(resolved.registration.has_value());
    REQUIRE(resolved.registration->status == EpochRegistration::Status::resolved);
    REQUIRE(resolved.outcome == CommitOutcome::updated);
    REQUIRE_FALSE(router.reorienting());
    REQUIRE(router.filled_count() == 3);
    REQUIRE(router.face_at(2) == Face::F);

    // Same tracked face, past the holdoff, now reading as a different
    // physical face: quarantined, slot preserved, strike recorded.
    const auto conflict = router.commit(Face::F, solid(kYellow), 12000);
    CHECK(conflict.outcome == CommitOutcome::quarantined);
    CHECK(router.filled_count() == 3);
    CHECK(centre_lab_distance(*router.slot_at(2), solid(kBlue)) == 0.0F);
    CHECK(router.strikes_at(2) == 1);
    // And an honest re-show of the newcomer still verifies in place.
    const auto reshow = router.commit(Face::F, drifted(solid(kBlue), 4.0F), 14000);
    CHECK(reshow.outcome == CommitOutcome::updated);
    CHECK(router.filled_count() == 3);
}

TEST_CASE("a mid-recovery guess never decides the roll of a captured slot", "[routing]") {
    // THE runD corruption, reproduced in the shape it actually took and then
    // refused. Measured on the real clip before this guard existed: pass 129
    // admitted body F into store key U on a guessed gauge; pass 188 then sent
    // body U to store key F, where verified_same_face - centre class, centre
    // Lab, census, every term rotation-invariant - waved it through (centre
    // Lab 2.72, census L1 2) and rewrote a good capture 180 degrees out. Ring
    // comparison against that slot's own earlier bytes: 2.13 mean Lab at 180,
    // 54.84 at identity, with runB and runC corroborating the earlier bytes.
    // Right label, rotated lie - exactly what relabel_capture's note warns of.
    //
    // The mechanism, and why the sweep: the guess is not merely used for the
    // newcomer's own write (that lands on an empty key and is harmless). It
    // becomes a PIN. One pass later the newcomer tight-matches its own slot,
    // registration resolves onto the lattice index that was guessed - and
    // with two consistent pins the candidate set is usually a singleton, so
    // the oriented roll tie-break never runs to catch it - and the verified
    // in-place update inherits a roll nothing ever tested. Withholding
    // provisional slots from registration is what closes it.
    //
    // Swept over all 24 true gauges x 6 newcomer labels because which
    // combinations turn into a silent roll rewrite depends on the arbitrary
    // lattice index adopt_gauge_direct happens to return; sampling one would
    // be sampling luck. Verified against pre-fix HEAD (10d2056): the sweep
    // fails there - see the commit body for the count of corrupting
    // combinations - and every combination leaves the slot byte-identical
    // with the guard in place.
    const CapturedFace epoch_u = patterned();
    const CapturedFace epoch_r = solid(kOrange);
    const CapturedFace newcomer = solid(kBlue);

    for (int true_gauge = 0; true_gauge < static_cast<int>(kLatticeSize); ++true_gauge) {
        // Build epoch 1's store as the epoch-2 readings would look after the
        // whole-cube rotation `true_gauge`, so the epoch-2 captures below are
        // the SAME physical faces seen from a different body frame - the only
        // construction in which "the roll came out wrong" is a statement
        // about the router rather than about the fixture.
        const auto store_u =
            kFacePermutation[static_cast<std::size_t>(true_gauge)][static_cast<std::size_t>(Face::U)];
        const auto store_r =
            kFacePermutation[static_cast<std::size_t>(true_gauge)][static_cast<std::size_t>(Face::R)];
        const CapturedFace stored_u = relabelled(epoch_u, Face::U, true_gauge);
        const CapturedFace stored_r = relabelled(epoch_r, Face::R, true_gauge);

        for (std::size_t label = 0; label < 6; ++label) {
            const auto newcomer_label = static_cast<Face>(label);
            CaptureRouter router;
            router.on_epoch(1);
            // The first commit takes the identity gauge (empty store) and the
            // second lands beside it, so both slots hold their bytes verbatim.
            REQUIRE(router.commit(store_u, stored_u, 0).outcome == CommitOutcome::filled);
            REQUIRE(router.commit(store_r, stored_r, 1000).outcome == CommitOutcome::filled);
            REQUIRE(router.face_at(0) == store_u);

            router.on_epoch(2);
            REQUIRE(router.reorienting());
            // A face the scan has never captured, admitted on a gauge chosen
            // with no evidence whatever.
            const auto admitted = router.commit(newcomer_label, newcomer, 10000);
            REQUIRE(admitted.registration.has_value());
            REQUIRE(admitted.registration->status == EpochRegistration::Status::insufficient);
            REQUIRE(admitted.outcome == CommitOutcome::filled);

            // ...and then the runD move: another tracked face carrying the
            // physical face already captured at store_u. Whatever the router
            // decides to do with it - update it in place at the roll the
            // oriented evidence supports, or refuse - the one outcome that is
            // not allowed is the slot changing to a rotation of itself.
            const auto reshow = router.commit(Face::U, epoch_u, 12000);
            REQUIRE(reshow.outcome != CommitOutcome::filled);  // no duplicate of a captured face

            const CapturedFace* content = router.slot_at(0);
            REQUIRE(content != nullptr);
            REQUIRE(router.face_at(0) == store_u);
            // One CHECK per combination rather than nine REQUIREs, so the
            // sweep reports every corrupting (gauge, label) pair instead of
            // aborting on the first.
            bool unchanged = true;
            for (std::size_t cell = 0; cell < stored_u.lab.size(); ++cell) {
                unchanged = unchanged && cv::norm(content->lab[cell] - stored_u.lab[cell]) == 0.0;
            }
            INFO("true gauge " << true_gauge << ", newcomer label " << label);
            CHECK(unchanged);
        }
    }
}

TEST_CASE("recovery still defers when the evidence contradicts itself", "[routing]") {
    // (c) The half of the old behaviour that is measurably RIGHT and must
    // survive: `ambiguous` means evidence exists and disagrees with itself,
    // and over runA..runD it separated true re-shows (best-roll 2.0-10.2
    // mean Lab) from false aliases (38-56) cleanly. Only `insufficient` -
    // no evidence at all, because the face was never captured - is
    // reinterpreted.
    //
    // Honestly labelled: this case passes on the PRE-FIX code too, by
    // construction. It is not a regression test for the fix, it is the
    // boundary test for the fix's SCOPE - it fails against the over-broad
    // implementation that treats every non-resolved status as a new face,
    // which is the mistake worth guarding against here. Verified by building
    // that variant (the `insufficient` test in commit() replaced by `false`):
    // both sections fail on it - the first fills a fourth slot on a pinned
    // but unresolved roll, the second fills and then quarantines the aliased
    // pair it should have refused to adjudicate at all.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::F, solid(kGreen), 2000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());

    SECTION("a lone re-show pins a face but not a roll") {
        // White tight-matches the stored white and nothing else, pinning
        // U -> U; the four rotations fixing U are indistinguishable on a
        // solid face, so the roll tie-break declines and content
        // equivalence declines (the other slots are distinguishable).
        const auto result = router.commit(Face::U, drifted(solid(kWhite), 3.0F), 10000);
        CHECK(result.outcome == CommitOutcome::deferred);
        REQUIRE(result.registration.has_value());
        CHECK(result.registration->status == EpochRegistration::Status::ambiguous);
        CHECK(router.reorienting());
        CHECK(router.filled_count() == 3);
    }

    SECTION("two epoch faces claiming one stored slot pin nothing") {
        CHECK(router.commit(Face::F, drifted(solid(kWhite), 3.0F), 10000).outcome == CommitOutcome::deferred);
        const auto second = router.commit(Face::D, drifted(solid(kWhite), 5.0F), 11000);
        CHECK(second.outcome == CommitOutcome::deferred);
        REQUIRE(second.registration.has_value());
        CHECK(second.registration->status == EpochRegistration::Status::ambiguous);
        CHECK(router.filled_count() == 3);
    }
}

TEST_CASE("the duplicate-centre net still guards mid-recovery admission", "[routing]") {
    // (d) The safety net that makes the new path admissible at all, and the
    // reason `insufficient` is not simply "let it through". A face whose
    // centre sits on a captured face's WITHOUT authenticating as it is slip
    // evidence, not a new face - identity claims one thing and colour claims
    // another - and writing it seeds the duplicate-slot wedge the legacy
    // centre-keyed store suffered.
    //
    // This case is discriminating in BOTH directions, which is the point of
    // pairing the two halves: the blue commit fails on the PRE-FIX code (it
    // deferred), and the suspect commits fail on a fix that skipped the
    // duplicate-centre guard (they would fill a third slot).
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::U, solid(kWhite), 0).outcome == CommitOutcome::filled);
    REQUIRE(router.commit(Face::R, solid(kRed), 1000).outcome == CommitOutcome::filled);

    router.on_epoch(2);
    REQUIRE(router.reorienting());

    // Centre inside the duplicate radius of the stored red, but three
    // stickers read green so the census is past reassoc_census (4): could be
    // the stored face badly lit, could be a different face aliasing it. Not
    // admissible either way.
    CapturedFace suspect = solid(kWashedOrangeAsRed);
    suspect = with_sticker(suspect, 0, kGreen);
    suspect = with_sticker(suspect, 1, kGreen);
    suspect = with_sticker(suspect, 2, kGreen);
    REQUIRE(centre_lab_distance(suspect, solid(kRed)) < RoutingConfig{}.duplicate_centre_radius);

    const auto refused = router.commit(Face::F, suspect, 10000);
    CHECK(refused.outcome == CommitOutcome::deferred);
    REQUIRE(refused.registration.has_value());
    CHECK(refused.registration->status == EpochRegistration::Status::insufficient);
    CHECK(router.filled_count() == 2);
    CHECK(router.reorienting());
    // Refusing touches no strike counter and pins no gauge, so a second
    // identical pass refuses again without escalating - the same property
    // adopt_gauge_direct's refusal has in the <= 1-slot regime.
    const auto retry = router.commit(Face::F, suspect, 12000);
    CHECK(retry.outcome == CommitOutcome::deferred);
    CHECK_FALSE(retry.force_lost);
    CHECK(router.filled_count() == 2);

    // The positive control, in the same fixture: a face that aliases NOTHING
    // stored is admitted on the very next pass. Without this the refusals
    // above would pass on the pre-fix code, which deferred everything.
    const auto admitted = router.commit(Face::D, solid(kBlue), 14000);
    CHECK(admitted.outcome == CommitOutcome::filled);
    CHECK(router.filled_count() == 3);
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kWhite)) == 0.0F);
    CHECK(centre_lab_distance(*router.slot_at(1), solid(kRed)) == 0.0F);
}

// ---------------------------------------------------------------------------
// The never-overwrite guarantee against the RE-MEASURED palette floor.
//
// kClosestCentrePairLab was 27.0 - above the closest pair the camera
// actually delivers (22.95 Lab, runD slot L orange against slot R red) - so
// the static_assert certifying same_face_centre_lab was certifying it
// against a bound the corpus contradicts, and the verifier's radius (25)
// sat OUTSIDE the real palette floor rather than inside it. These cases pin
// the corrected relationship at both ends.
// ---------------------------------------------------------------------------
TEST_CASE("the verify radius sits inside the measured closest centre pair", "[routing]") {
    // The structural statement, independent of the current numbers: no
    // centre offset can both authenticate an in-place update AND be a
    // legitimate difference between two of the cube's own centres.
    STATIC_REQUIRE(RoutingConfig{}.same_face_centre_lab < kClosestCentrePairLab);
    STATIC_REQUIRE(RoutingConfig{}.duplicate_centre_radius < kClosestCentrePairLab);
    STATIC_REQUIRE(kMinCentreSeedSeparationLab < kClosestCentrePairLab);
    // The measurement itself: 22.95 is the closest DIFFERENT-name centre
    // pair over the eleven recordings, so the ceiling may not exceed it.
    STATIC_REQUIRE(kClosestCentrePairLab <= 22.95F);

    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, solid(kRed), 0).outcome == CommitOutcome::filled);

    // A re-show 24 Lab off the stored centre. Same centre class, identical
    // census - so class and census both say "same face" - and 24 sits in
    // the window the old radius admitted (24 <= 25) but the real palette
    // floor does not (24 > 22.95). Under the stale ceiling this was an
    // `updated`: nine stored stickers overwritten on an authentication the
    // palette cannot support.
    const CapturedFace far_reshow = drifted(solid(kRed), 24.0F);
    REQUIRE(centre_lab_distance(far_reshow, solid(kRed)) == 24.0F);
    REQUIRE(census_l1_distance(color_census(far_reshow), color_census(solid(kRed))) == 0);

    const auto result = router.commit(Face::F, far_reshow, 2000);
    CHECK(result.outcome == CommitOutcome::quarantined);
    // The slot keeps what was verified when it was written.
    CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
}

TEST_CASE("an honest same-face drift inside the radius still updates", "[routing]") {
    // The other end: tightening the radius must not have closed the ordinary
    // re-show. 20 Lab of lighting drift is well inside real behaviour and
    // still verifies.
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, solid(kRed), 0).outcome == CommitOutcome::filled);

    const CapturedFace reshow = drifted(solid(kRed), 20.0F);
    REQUIRE(centre_lab_distance(reshow, solid(kRed)) == 20.0F);
    CHECK(router.commit(Face::F, reshow, 2000).outcome == CommitOutcome::updated);
}
