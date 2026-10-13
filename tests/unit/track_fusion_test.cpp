// Shared tracked-scan fusion (track_fusion.hpp, P6.12): the ONE
// implementation of the sub-epoch feed, its takeover detector, the
// content-based cross-epoch registration and best-sub-epoch selection
// that both the offline runner and the live app's track mode run. The
// corpus replay is the deep acceptance surface for the bars themselves
// (they were measured there); these cases pin the structural contracts a
// refactor could silently break - the sub-epoch keying scheme, the
// generation split, a clean content merge, never-overwrite, and (P6.15)
// the cross-slot invariant that one physical face may not occupy two
// labels of one era.

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/track_fusion.hpp>

#include <opencv2/core.hpp>

#include <map>

using namespace cube::vision;
namespace fusion = cube::vision::fusion;

namespace {

// Synthetic per-face content: hues 60 degrees apart at chroma 40 (any two
// faces sit far outside the registration radius and the verify backstop),
// with a strong per-cell asymmetry so no in-plane roll of a face against
// itself is cost-free - a solid face is rotationally symmetric, and the
// margin bar would (rightly) refuse the resulting ambiguity.
[[nodiscard]] cv::Vec3f cell_lab(int face, int cell) {
    static const std::array<std::array<float, 2>, 6> kBase{{{40.0F, 0.0F},
                                                            {20.0F, 34.6F},
                                                            {-20.0F, 34.6F},
                                                            {-40.0F, 0.0F},
                                                            {-20.0F, -34.6F},
                                                            {20.0F, -34.6F}}};
    const float col = static_cast<float>(cell % 3 - 1);
    const float row = static_cast<float>(cell / 3 - 1);
    return {55.0F, kBase[static_cast<std::size_t>(face)][0] + 14.0F * col,
            kBase[static_cast<std::size_t>(face)][1] + 18.0F * row};
}

[[nodiscard]] FaceObservation observation_of(int face) {
    FaceObservation observation;
    for (int cell = 0; cell < 9; ++cell) {
        observation.lab[static_cast<std::size_t>(cell)] = cell_lab(face, cell);
        observation.cell_purity[static_cast<std::size_t>(cell)] = 1.0F;
    }
    return observation;
}

// The SAME physical face read a second time, 10 Lab away - inside
// kCrossSlotDuplicateLab (12, the seed floor), so the duplication tests
// below are testing the bar rather than exact equality, and the two reads
// still cost enough against each other (9.4 in the registration cell
// metric) to leave the merge's margin intact. Exact copies would tie the
// candidate selection at cost 0 and refuse the merge outright - which is
// itself worth knowing: a duplicated era does not only corrupt the store
// it lands in, it also poisons the relabel it is scored through.
[[nodiscard]] FaceObservation second_read_of(int face) {
    FaceObservation observation = observation_of(face);
    for (auto& lab : observation.lab) {
        lab[2] += 10.0F;
    }
    return observation;
}

}  // namespace

TEST_CASE("fusion feed keys sub-epochs and splits a generation on a centre takeover",
          "[vision][fusion]") {
    fusion::FusionStore store;
    std::map<int, int> generations;

    // Two settled observations of face 0 in gauge epoch 5.
    auto first = fusion::add_observation(store, generations, 5, 0, observation_of(0));
    CHECK(first.sub_epoch == 5000);
    CHECK_FALSE(first.generation_split);
    (void)fusion::add_observation(store, generations, 5, 0, observation_of(0));
    REQUIRE(store.count(5000) == 1);
    CHECK(store[5000][0].observation_count() == 2);

    // An incoming centre a whole hue sector away (>> kTakeoverJumpLab)
    // under the SAME label means a different physical face took the label
    // over: the frozen sub-epoch keeps its evidence, the feed opens the
    // next generation.
    const auto takeover = fusion::add_observation(store, generations, 5, 0, observation_of(3));
    CHECK(takeover.generation_split);
    CHECK(takeover.sub_epoch == 5001);
    CHECK(store[5000][0].observation_count() == 2);
    CHECK(store[5001][0].observation_count() == 1);

    // A different gauge epoch starts at generation 0 independently.
    const auto other_epoch = fusion::add_observation(store, generations, 7, 2, observation_of(2));
    CHECK(other_epoch.sub_epoch == 7000);
}

// P6.15. The slip that motivated the cross-slot invariant is invisible
// HERE, and pinning that is the point: a physical face arriving under a
// second label whose slot is still empty has no incumbent to disagree
// with, so the feed accepts it and the era ends up holding one face
// twice. The invariant lives at the transplant and at selection
// (track_fusion.hpp records why the feed-time variant was measured and
// rejected); this case is the honest statement of what the feed does NOT
// catch, so a future reader does not go looking for a guarantee that is
// not there.
TEST_CASE("fusion feed cannot see a face arriving under a second, empty label",
          "[vision][fusion]") {
    fusion::FusionStore store;
    std::map<int, int> generations;

    (void)fusion::add_observation(store, generations, 5, 0, observation_of(0));
    (void)fusion::add_observation(store, generations, 5, 0, observation_of(0));
    REQUIRE(store[5000][0].observation_count() == 2);

    const auto duplicate = fusion::add_observation(store, generations, 5, 2, observation_of(0));
    CHECK_FALSE(duplicate.generation_split);
    CHECK(duplicate.sub_epoch == 5000);
    CHECK(fusion::distinct_converged_faces(store[5000]) == 0);  // nothing converged yet
}

TEST_CASE("registration merges shared content across epochs without overwriting settled faces",
          "[vision][fusion]") {
    fusion::FusionStore store;
    std::map<int, int> generations;

    // Older epoch: all six faces converged (kMinObservations identical
    // reads each - zero spread converges by construction).
    for (int face = 0; face < 6; ++face) {
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            (void)fusion::add_observation(store, generations, 1, face, observation_of(face));
        }
    }
    // Newer epoch: three of the same physical faces re-seen after an
    // unstitchable drop, here under the identity relabel, each at the
    // registration scoring floor (2 agreeing reads - thin content that
    // may carry the score but must never displace settled evidence).
    for (int face : {0, 2, 4}) {
        for (int obs = 0; obs < fusion::kRegisterScoreFloor; ++obs) {
            (void)fusion::add_observation(store, generations, 2, face, observation_of(face));
        }
    }
    REQUIRE(store.size() == 2);

    const auto stats = fusion::register_epochs(store);
    CHECK(stats.merges == 1);
    CHECK(stats.refusals == 0);
    REQUIRE(store.size() == 1);
    REQUIRE(store.count(1000) == 1);

    // Never-overwrite: the settled older windows kept their full
    // evidence; the thin transplants were discarded, not merged in.
    for (int face = 0; face < 6; ++face) {
        CHECK(store[1000][face].observation_count() == FaceAggregator::kMinObservations);
        CHECK(store[1000][face].converged());
    }

    const auto best = fusion::best_sub_epoch(store);
    CHECK(best.sub_epoch == 1000);
    CHECK(best.observed == 6);
    CHECK(best.converged == 6);
    CHECK(best.distinct == 6);
}

// P6.15: registration is the other way a duplicate can be built - a
// transplant into an empty label of an epoch that already holds that
// colour. The merge still commits; only the duplicating face is left
// behind.
TEST_CASE("registration commits a merge but refuses the transplant that would duplicate a face",
          "[vision][fusion]") {
    fusion::FusionStore store;
    // Older epoch holds faces 0 and 1; label 2 is empty.
    for (int face : {0, 1}) {
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            store[1000][face].add(observation_of(face));
        }
    }
    // Newer epoch shares face 1 (the merge's anchor) and offers, under
    // label 2, a second read of the older epoch's face 0.
    for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
        store[2000][1].add(observation_of(1));
        store[2000][2].add(second_read_of(0));
    }

    const auto stats = fusion::register_epochs(store);
    CHECK(stats.merges == 1);
    REQUIRE(store.size() == 1);
    REQUIRE(store.count(1000) == 1);
    // The merge happened; the duplicating transplant did not.
    CHECK(store[1000].count(2) == 0);
    CHECK(fusion::best_sub_epoch(store).distinct == 2);
}

// Which copy survives is decided by evidence, not by label number: the
// transplant pass runs strongest-first, so the weaker copy of a
// duplicated colour is the one refused.
TEST_CASE("registration keeps the better-supported copy when one epoch offers a face twice",
          "[vision][fusion]") {
    fusion::FusionStore store;
    for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
        store[1000][1].add(observation_of(1));  // the merge's anchor
    }
    // The newer epoch holds ONE physical face under labels 0 and 2 - label
    // 0 thin, label 2 fully settled. Label 0 sorts first; evidence does
    // not. (live-demo-p6-retry's sub-epoch 1002, in miniature.)
    for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
        store[2000][1].add(observation_of(1));
        store[2000][2].add(observation_of(0));
    }
    for (int obs = 0; obs < fusion::kRegisterScoreFloor; ++obs) {
        store[2000][0].add(second_read_of(0));
    }

    CHECK(fusion::register_epochs(store).merges == 1);
    REQUIRE(store.count(1000) == 1);
    CHECK(store[1000].count(2) == 1);
    CHECK(store[1000][2].observation_count() == FaceAggregator::kMinObservations);
    CHECK(store[1000].count(0) == 0);
}

// The same shape with genuinely new content: the transplant lands. This
// is the control that keeps the refusal above from reading as "P6.15
// stopped transplanting".
TEST_CASE("registration still transplants a face the target epoch does not already hold",
          "[vision][fusion]") {
    fusion::FusionStore store;
    for (int face : {0, 1}) {
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            store[1000][face].add(observation_of(face));
        }
    }
    for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
        store[2000][1].add(observation_of(1));
        store[2000][2].add(observation_of(2));
    }

    const auto stats = fusion::register_epochs(store);
    CHECK(stats.merges == 1);
    REQUIRE(store.count(1000) == 1);
    CHECK(store[1000].count(2) == 1);
    CHECK(fusion::best_sub_epoch(store).distinct == 3);
}

// P6.15's completion bar: six converged slots are not six faces when two
// of them hold the same one. The store is built directly because the feed
// now refuses to build it - this is the shape a pre-P6.15 session (or a
// future path that bypasses add_observation) can still hand finalize.
TEST_CASE("best_sub_epoch counts one physical face once", "[vision][fusion]") {
    fusion::FusionStore store;
    for (int face = 0; face < 6; ++face) {
        // Labels 2 and 3 both duplicate label 0: six converged slots, four
        // physical faces.
        const int content = (face == 2 || face == 3) ? 0 : face;
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            store[1000][face].add(observation_of(content));
        }
    }

    CHECK(fusion::distinct_converged_faces(store[1000]) == 4);
    const auto best = fusion::best_sub_epoch(store);
    CHECK(best.sub_epoch == 1000);
    CHECK(best.converged == 6);
    CHECK(best.distinct == 4);

    // Distinct leads the ranking: a five-face era outranks a six-slot era
    // holding four faces, because only the former is one face from a cube.
    for (int face = 0; face < 5; ++face) {
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            store[2000][face].add(observation_of(face));
        }
    }
    const auto reranked = fusion::best_sub_epoch(store);
    CHECK(reranked.sub_epoch == 2000);
    CHECK(reranked.converged == 5);
    CHECK(reranked.distinct == 5);
}

TEST_CASE("best_sub_epoch reports no evidence as -1 and prefers converged over merely observed",
          "[vision][fusion]") {
    fusion::FusionStore store;
    CHECK(fusion::best_sub_epoch(store).sub_epoch == -1);

    std::map<int, int> generations;
    // Sub-epoch 1000: two faces converged. Sub-epoch 3000: three faces at
    // a single observation each (observed nothing at the capture bar).
    for (int face : {0, 1}) {
        for (std::size_t obs = 0; obs < FaceAggregator::kMinObservations; ++obs) {
            (void)fusion::add_observation(store, generations, 1, face, observation_of(face));
        }
    }
    for (int face : {2, 3, 4}) {
        (void)fusion::add_observation(store, generations, 3, face, observation_of(face));
    }
    const auto best = fusion::best_sub_epoch(store);
    CHECK(best.sub_epoch == 1000);
    CHECK(best.observed == 2);
    CHECK(best.converged == 2);
}
