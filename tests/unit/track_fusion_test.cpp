// Shared tracked-scan fusion (track_fusion.hpp, P6.12): the ONE
// implementation of the sub-epoch feed, its takeover detector, the
// content-based cross-epoch registration and best-sub-epoch selection
// that both the offline runner and the live app's track mode run. The
// corpus replay is the deep acceptance surface for the bars themselves
// (they were measured there); these cases pin the structural contracts a
// refactor could silently break - the sub-epoch keying scheme, the
// generation split, a clean content merge, and never-overwrite.

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

}  // namespace

TEST_CASE("fusion feed keys sub-epochs and splits a generation on a centre takeover", "[vision][fusion]") {
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
