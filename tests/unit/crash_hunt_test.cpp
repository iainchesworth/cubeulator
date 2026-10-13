// Crash-hunt harness - hidden tag, never part of the normal suite.
//
// Drives CaptureRouter's whole public state machine (commit / on_epoch /
// clear_slot / clear) under randomized adversarial op sequences and
// asserts, after EVERY op, the structural invariant the rest of the system
// dereferences unchecked:
//
//   for every fill index i in [0, filled_count()):
//     slots_[fill_order_[i]] is engaged
//
// That invariant is what makes adopt_gauge_direct's
// `slots_[fill_order_.front()]->captured`, commit_direct's duplicate-centre
// scan, commit()'s store_view build, and ScanSession::finalizeIfComplete's
// `*router_.slot_at(i)` safe. slot_at() returns nullptr exactly when it is
// violated, so probing every index is a direct, non-invasive witness.
//
// Also fuzzed, because they are the paths an audit flagged as the only
// candidates for breaking it: retakes that empty the store mid-recovery,
// epoch changes immediately after a commit, epoch numbers that REPEAT or
// go BACKWARDS (ScanSession::resetReading re-inits the tracker's epoch
// counter without clearing the router, so the router can legitimately be
// handed an epoch it has already seen), conflicting content right after a
// quarantine, and out-of-range clear_slot indices.
//
//   cube_unit_tests.exe "[.crash-hunt]"

#include <catch2/catch_test_macros.hpp>

#include <cube/model/face.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/orientation_lattice.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace cube::vision;
using cube::model::Face;

namespace {

// Same palette the [routing] suite uses, plus the aliasing pair that
// drives the duplicate-centre and verification-conflict branches.
const std::array<cv::Vec3f, 8> kPalette{{
    {96.0F, 2.0F, 2.0F},     // white
    {88.0F, 5.0F, 80.0F},    // yellow
    {45.0F, 60.0F, 35.0F},   // red
    {65.0F, 40.0F, 57.0F},   // orange
    {50.0F, -50.0F, 45.0F},  // green
    {35.0F, 12.0F, -60.0F},  // blue
    {50.0F, 55.0F, 40.0F},   // washed orange, aliases red's centre
    {47.0F, 58.0F, 37.0F},   // near-red, inside the duplicate radius
}};

[[nodiscard]] CapturedFace solid(const cv::Vec3f& lab) {
    CapturedFace face;
    face.lab.fill(lab);
    return face;
}

// A face whose census differs from `solid(lab)` by 2*swaps - the material
// that drives verification conflicts, strikes and the layer-turn signature.
[[nodiscard]] CapturedFace speckled(const cv::Vec3f& lab, const cv::Vec3f& other, int swaps) {
    CapturedFace face = solid(lab);
    for (int i = 0; i < swaps && i < 9; ++i) {
        face.lab[static_cast<std::size_t>(i)] = other;
    }
    return face;
}

// The structural invariant, probed through the public accessors: slot_at()
// returns nullptr precisely when fill_order_ names a disengaged slot, and
// face_at() must agree with it index for index. Fill order must also carry
// no duplicate face - a duplicate would make filled_count() overcount the
// engaged slots and walk finalizeIfComplete off the end.
void require_store_consistent(const CaptureRouter& router, const std::string& trace) {
    const int filled = router.filled_count();
    INFO(trace);
    REQUIRE(filled >= 0);
    REQUIRE(filled <= 6);
    std::array<bool, 6> seen{};
    for (int i = 0; i < filled; ++i) {
        INFO("fill index " << i << " of " << filled);
        const auto face = router.face_at(i);
        REQUIRE(face.has_value());
        REQUIRE(!seen[static_cast<std::size_t>(*face)]);
        seen[static_cast<std::size_t>(*face)] = true;
        REQUIRE(router.slot_at(i) != nullptr);
    }
    // Past the filled range both accessors must be empty, never engaged.
    REQUIRE(router.slot_at(filled) == nullptr);
    REQUIRE_FALSE(router.face_at(filled).has_value());
    REQUIRE(router.slot_at(-1) == nullptr);
}

}  // namespace

TEST_CASE("randomized adversarial routing sequences keep the slot store consistent",
          "[.crash-hunt]") {
    constexpr int kSeeds = 4000;
    constexpr int kOpsPerSeed = 120;

    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(static_cast<std::uint32_t>(seed));
        CaptureRouter router{RoutingConfig{}};
        std::int64_t now_ms = 0;
        int epoch = 0;
        std::string trace = "seed=" + std::to_string(seed);

        require_store_consistent(router, trace + " op=init");

        for (int op = 0; op < kOpsPerSeed; ++op) {
            // Time mostly advances past the recommit holdoff so updates and
            // conflicts actually land, but sometimes does not, to exercise
            // the holdoff branch.
            now_ms += (rng() % 4 == 0) ? 10 : 1000;
            const int choice = static_cast<int>(rng() % 100);
            trace = "seed=" + std::to_string(seed) + " op=" + std::to_string(op) +
                    " choice=" + std::to_string(choice);

            if (choice < 60) {
                // Commit a random face with random content. The palette's
                // aliasing entries make duplicate-centre quarantines,
                // verification conflicts and strikes common rather than rare.
                const auto face = static_cast<Face>(rng() % 6);
                const auto& lab = kPalette[rng() % kPalette.size()];
                const auto& other = kPalette[rng() % kPalette.size()];
                const int swaps = static_cast<int>(rng() % 5);
                (void)router.commit(face, speckled(lab, other, swaps), now_ms);
            } else if (choice < 78) {
                // Epoch churn, including repeats and backwards steps: the
                // tracker's counter is re-initialised by resetReading()
                // without the router being cleared, so an already-seen
                // epoch number is reachable live.
                switch (rng() % 4) {
                    case 0:
                        ++epoch;
                        break;
                    case 1:
                        break;  // repeat
                    case 2:
                        epoch = std::max(0, epoch - 1);
                        break;  // backwards
                    default:
                        epoch = static_cast<int>(rng() % 3);
                        break;
                }
                router.on_epoch(epoch);
            } else if (choice < 92) {
                // Retakes, including indices that name no slot and indices
                // that empty the store mid-recovery.
                const int index = static_cast<int>(rng() % 9) - 1;  // -1..7
                (void)router.clear_slot(index);
            } else if (choice < 96) {
                router.clear();
            } else {
                // Read-only accessors QML polls at arbitrary moments.
                (void)router.reorienting();
                for (int i = -1; i <= 7; ++i) {
                    (void)router.strikes_at(i);
                    (void)router.slot_at(i);
                    (void)router.face_at(i);
                }
            }

            require_store_consistent(router, trace);
        }
    }
}

TEST_CASE("targeted adversarial sequences the audit named", "[.crash-hunt]") {
    const auto red = solid(kPalette[2]);
    const auto green = solid(kPalette[4]);
    const auto blue = solid(kPalette[5]);
    const auto white = solid(kPalette[0]);

    SECTION("retake empties the store mid-recovery, then a commit adopts a gauge directly") {
        CaptureRouter router;
        router.on_epoch(1);
        (void)router.commit(Face::U, red, 1000);
        (void)router.commit(Face::R, green, 2000);
        (void)router.commit(Face::F, blue, 3000);
        REQUIRE(router.filled_count() == 3);
        router.on_epoch(2);  // >= 2 filled -> recovery
        REQUIRE(router.reorienting());
        // Empty the store from under recovery, one slot at a time.
        while (router.filled_count() > 0) {
            require_store_consistent(router, "mid-retake");
            REQUIRE(router.clear_slot(0));
        }
        require_store_consistent(router, "emptied");
        REQUIRE_FALSE(router.reorienting());
        // The adopt-gauge-direct path with an empty fill order.
        (void)router.commit(Face::D, white, 4000);
        require_store_consistent(router, "post-adopt-empty");
    }

    SECTION("epoch change immediately after a commit, repeatedly, with one slot") {
        CaptureRouter router;
        for (int e = 1; e <= 40; ++e) {
            router.on_epoch(e);
            (void)router.commit(static_cast<Face>(e % 6), red, 1000L * e);
            require_store_consistent(router, "epoch " + std::to_string(e));
        }
    }

    SECTION("epoch number repeats after a store-emptying retake") {
        CaptureRouter router;
        router.on_epoch(7);
        (void)router.commit(Face::U, red, 1000);
        (void)router.commit(Face::R, green, 2000);
        router.on_epoch(8);
        REQUIRE(router.clear_slot(0));
        REQUIRE(router.clear_slot(0));
        // resetReading() re-inits the tracker's epoch counter; the router
        // keeps its own current_epoch_, so it can be handed 7 again.
        router.on_epoch(7);
        (void)router.commit(Face::B, blue, 5000);
        require_store_consistent(router, "epoch replay");
    }

    SECTION("conflicting content right after a duplicate-centre quarantine") {
        CaptureRouter router;
        router.on_epoch(1);
        (void)router.commit(Face::U, red, 1000);
        (void)router.commit(Face::R, solid(kPalette[7]), 2000);  // near-red: duplicate quarantine
        require_store_consistent(router, "post-duplicate");
        (void)router.commit(Face::U, speckled(kPalette[4], kPalette[0], 9), 3000);  // conflict
        require_store_consistent(router, "post-conflict");
        (void)router.commit(Face::R, speckled(kPalette[1], kPalette[0], 9),
                            4000);  // second face conflict
        require_store_consistent(router, "post-layer-turn");
    }

    SECTION("six slots then a failed-assembly drop, repeatedly") {
        CaptureRouter router;
        router.on_epoch(1);
        for (int i = 0; i < 6; ++i) {
            (void)router.commit(static_cast<Face>(i), solid(kPalette[static_cast<std::size_t>(i)]),
                                1000L * (i + 1));
        }
        REQUIRE(router.filled_count() == 6);
        for (int drop = 0; drop < 6; ++drop) {
            const int filled = router.filled_count();
            REQUIRE(router.clear_slot(filled - 1));
            require_store_consistent(router, "drop " + std::to_string(drop));
        }
    }
}
