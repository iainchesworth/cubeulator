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

    SECTION("a brand-new face defers until other faces are recognised") {
        // Nothing stored looks like blue: committing it to an empty slot
        // now would gamble on an unresolved gauge, so it buffers...
        const auto unknown = router.commit(Face::D, solid(kBlue), 10000);
        CHECK(unknown.outcome == CommitOutcome::deferred);
        CHECK(router.filled_count() == 3);
        // ...until positive re-associations resolve the gauge (identity
        // here), at which point the buffered new face flushes into an
        // empty slot alongside the updates.
        CHECK(router.commit(Face::U, drifted(solid(kWhite), 3.0F), 11000).outcome == CommitOutcome::deferred);
        const auto resolved = router.commit(Face::R, drifted(solid(kRed), 3.0F), 12000);
        CHECK(resolved.outcome == CommitOutcome::updated);
        REQUIRE(resolved.confirmed_slots.size() == 3);
        CHECK(router.filled_count() == 4);
        CHECK_FALSE(router.reorienting());
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

TEST_CASE("an epoch change with at most one slot re-associates immediately", "[routing]") {
    CaptureRouter router;
    router.on_epoch(1);
    REQUIRE(router.commit(Face::F, solid(kRed), 0).outcome == CommitOutcome::filled);

    // Loss and re-anchor with one slot: no recovery buffering (identity is
    // definitional), the next commit adopts a gauge on the spot.
    router.on_epoch(2);
    CHECK_FALSE(router.reorienting());

    SECTION("a tight content match re-keys the stored slot under the new frame") {
        const auto result = router.commit(Face::U, drifted(solid(kRed), 3.0F), 2000);
        CHECK(result.outcome == CommitOutcome::updated);
        REQUIRE(result.confirmed_slots.size() == 1);
        CHECK(result.confirmed_slots.front() == 0);
        CHECK(router.filled_count() == 1);
    }

    SECTION("a clearly different face files beside the stored slot, not on it") {
        const auto result = router.commit(Face::F, solid(kBlue), 2000);
        CHECK(result.outcome == CommitOutcome::filled);
        CHECK(router.filled_count() == 2);
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
    }

    SECTION("a near-duplicate centre that fails the tight match quarantines") {
        // Centre within the duplicate radius of the stored red, but three
        // stickers read green (census L1 = 6, past the re-association
        // drift): could be the stored face badly lit, could be a different
        // face aliasing it - the router adopts nothing and writes nothing.
        CapturedFace suspect = solid(kWashedOrangeAsRed);
        suspect = with_sticker(suspect, 0, kGreen);
        suspect = with_sticker(suspect, 1, kGreen);
        suspect = with_sticker(suspect, 2, kGreen);
        const auto result = router.commit(Face::U, suspect, 2000);
        CHECK(result.outcome == CommitOutcome::quarantined);
        CHECK(result.duplicate_centre);
        CHECK(router.filled_count() == 1);
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
    }
}

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
    // on_epoch's own identity-is-definitional regime, so recovery must end
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
    CHECK(router.commit(Face::F, solid(kBlue), 10000).outcome == CommitOutcome::deferred);

    SECTION("down to one slot: identity is definitional again") {
        REQUIRE(router.clear_slot(0));  // retake white
        CHECK_FALSE(router.reorienting());
        const auto result = router.commit(Face::F, solid(kBlue), 11000);
        CHECK(result.outcome == CommitOutcome::filled);
        CHECK(router.filled_count() == 2);
        // The surviving slot's content was never touched by the exit.
        CHECK(centre_lab_distance(*router.slot_at(0), solid(kRed)) == 0.0F);
    }

    SECTION("down to zero slots: everything is fresh") {
        REQUIRE(router.clear_slot(0));
        REQUIRE(router.clear_slot(0));
        CHECK_FALSE(router.reorienting());
        const auto result = router.commit(Face::F, solid(kBlue), 11000);
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
        REQUIRE(centre_lab_distance(pattern, aliasing) < config.reassoc_centre_lab);

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
