#include <catch2/catch_test_macros.hpp>

#include <cube/vision/temporal_fusion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

using namespace cube::vision;

namespace {

// Nine distinct but arbitrary Lab colours - values only need to be stable
// across frames, not physically meaningful.
std::array<cv::Vec3f, 9> base_colors() {
    std::array<cv::Vec3f, 9> lab{};
    for (int i = 0; i < 9; ++i) {
        const auto f = static_cast<float>(i);
        lab[static_cast<std::size_t>(i)] =
            cv::Vec3f(30.0F + 5.0F * f, -20.0F + 4.0F * f, 10.0F + 3.0F * f);
    }
    return lab;
}

FaceObservation observation_with(const std::array<cv::Vec3f, 9>& lab) {
    FaceObservation observation;
    observation.lab = lab;
    observation.geometry_confidence = 1.0F;
    return observation;
}

// Mirrors the aggregator's median definition (midpoint of the two central
// values for even counts) so expected captures can be compared exactly.
float median_of(std::vector<float> values) {
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if (values.size() % 2 != 0) {
        return values[mid];
    }
    return (values[mid - 1] + values[mid]) * 0.5F;
}

}  // namespace

TEST_CASE("FaceAggregator converges on identical observations and captures them", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    for (std::size_t i = 0; i < FaceAggregator::kMinObservations; ++i) {
        REQUIRE_FALSE(aggregator.converged());
        aggregator.add(observation_with(lab));
    }

    REQUIRE(aggregator.observation_count() == FaceAggregator::kMinObservations);
    REQUIRE(aggregator.converged());

    const CapturedFace face = aggregator.capture();
    for (std::size_t cell = 0; cell < 9; ++cell) {
        for (int channel = 0; channel < 3; ++channel) {
            REQUIRE(std::abs(face.lab[cell][channel] - lab[cell][channel]) < 1e-4F);
        }
    }
}

TEST_CASE("FaceAggregator never converges on high-variance input", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    // A 3-phase L-channel flicker (-24 / 0 / +24) plus small seeded jitter:
    // any window of >= kMinObservations consecutive frames mixes at least
    // two phases 24 apart, keeping every cell's L-channel MAD far above the
    // accept threshold no matter how the ring's eviction lines up. All nine
    // cells unstable is far beyond the kMaxUnstableCells glare allowance,
    // so this must never converge.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> jitter(-2.0F, 2.0F);
    constexpr std::array<float, 3> kPhaseOffsets{-24.0F, 0.0F, 24.0F};

    for (std::size_t frame = 0; frame < 2 * FaceAggregator::kWindowSize; ++frame) {
        auto flickered = lab;
        const float offset = kPhaseOffsets[frame % kPhaseOffsets.size()];
        for (auto& color : flickered) {
            color[0] += offset + jitter(rng);
        }
        aggregator.add(observation_with(flickered));
        REQUIRE_FALSE(aggregator.converged());
    }
}

TEST_CASE("FaceAggregator converges despite one glare-unstable cell", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    // Cell 4 plays a glossy tile with a specular highlight dancing on it:
    // large sign-alternating swings plus seeded jitter keep its MAD far
    // above the accept threshold in every window, while the other eight
    // cells stay rock steady. The deterministic swing component (rather
    // than pure random jitter) guarantees the instability regardless of how
    // the standard library implements uniform_real_distribution.
    constexpr std::size_t kGlareCell = 4;
    constexpr std::array<float, 4> kGlareSwing{28.0F, -33.0F, 40.0F, -25.0F};
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> jitter(-2.0F, 2.0F);

    std::array<std::vector<float>, 3> glare_fed{};
    for (std::size_t i = 0; i < FaceAggregator::kMinObservations; ++i) {
        REQUIRE_FALSE(aggregator.converged());
        auto frame = lab;
        for (int channel = 0; channel < 3; ++channel) {
            frame[kGlareCell][channel] += kGlareSwing[i % kGlareSwing.size()] + jitter(rng);
            glare_fed[static_cast<std::size_t>(channel)].push_back(frame[kGlareCell][channel]);
        }
        aggregator.add(observation_with(frame));
    }

    // Premise check: the fed glare values really are unstable in the
    // aggregator's own terms (MAD above the 6.0 Lab accept threshold), so
    // this exact input would have deadlocked under the old all-cells gate.
    {
        const float center = median_of(glare_fed[0]);
        std::vector<float> deviations;
        deviations.reserve(glare_fed[0].size());
        for (const float value : glare_fed[0]) {
            deviations.push_back(std::abs(value - center));
        }
        REQUIRE(median_of(deviations) > 6.0F);
    }

    // Eight agreeing cells are enough: one glare cell can no longer hold
    // off capture, and the progress ring reads full.
    REQUIRE(aggregator.converged());
    REQUIRE(aggregator.convergence() >= 1.0F - 1e-6F);

    const CapturedFace face = aggregator.capture();
    for (std::size_t cell = 0; cell < 9; ++cell) {
        if (cell == kGlareCell) {
            continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
            REQUIRE(std::abs(face.lab[cell][channel] - lab[cell][channel]) < 1e-4F);
        }
    }

    // The glare cell reports the median of what was actually fed - its
    // most-supported reading across the window, not a stale or zeroed slot.
    for (int channel = 0; channel < 3; ++channel) {
        const float expected = median_of(glare_fed[static_cast<std::size_t>(channel)]);
        REQUIRE(std::abs(face.lab[kGlareCell][channel] - expected) < 1e-4F);
    }
}

TEST_CASE("FaceAggregator does not converge when too many cells are unstable", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    // Four flickering cells exceed the kMaxUnstableCells allowance, so the
    // Nth-best-cell gate must stay shut: this is the occlusion/blur regime
    // (many cells disturbed at once), not tolerable glare.
    constexpr std::array<std::size_t, 4> kUnstableCells{0, 2, 5, 8};
    static_assert(kUnstableCells.size() > FaceAggregator::kMaxUnstableCells);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> jitter(-2.0F, 2.0F);
    constexpr std::array<float, 3> kPhaseOffsets{-24.0F, 0.0F, 24.0F};

    for (std::size_t frame = 0; frame < 2 * FaceAggregator::kWindowSize; ++frame) {
        auto flickered = lab;
        const float offset = kPhaseOffsets[frame % kPhaseOffsets.size()];
        for (const std::size_t cell : kUnstableCells) {
            flickered[cell][0] += offset + jitter(rng);
        }
        aggregator.add(observation_with(flickered));
        REQUIRE_FALSE(aggregator.converged());
    }
}

TEST_CASE("FaceAggregator reset clears accumulated state", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    for (std::size_t i = 0; i < FaceAggregator::kMinObservations; ++i) {
        aggregator.add(observation_with(lab));
    }
    REQUIRE(aggregator.converged());

    aggregator.reset();
    REQUIRE(aggregator.observation_count() == 0);
    REQUIRE(aggregator.convergence() == 0.0F);
    REQUIRE_FALSE(aggregator.converged());

    // A reset aggregator behaves like a fresh one: convergence is earned
    // again from scratch, not resumed.
    for (std::size_t i = 0; i < FaceAggregator::kMinObservations; ++i) {
        REQUIRE_FALSE(aggregator.converged());
        aggregator.add(observation_with(lab));
    }
    REQUIRE(aggregator.converged());
}

TEST_CASE("FaceAggregator convergence grows from 0 to 1 on steady input", "[vision]") {
    FaceAggregator aggregator;
    const auto lab = base_colors();

    REQUIRE(aggregator.convergence() == 0.0F);

    float previous = 0.0F;
    for (std::size_t i = 0; i < FaceAggregator::kMinObservations; ++i) {
        aggregator.add(observation_with(lab));
        const float progress = aggregator.convergence();
        REQUIRE(progress >= previous);
        REQUIRE(progress <= 1.0F);
        previous = progress;
    }

    REQUIRE(aggregator.converged());
    REQUIRE(previous >= 1.0F - 1e-6F);
}

// capture() is public and [[nodiscard]] with no guard of its own. Before
// this was fixed, calling it on a fresh (or just-reset) aggregator
// dereferenced a default-constructed empty vector - whose data() is
// nullptr - inside median(): `values[0]` on size 0, followed by
// std::max_element over an empty range and a dereference of the returned
// end iterator. Two out-of-bounds reads, the first of which faults.
TEST_CASE("FaceAggregator::capture on an empty window is defined", "[vision]") {
    const FaceAggregator fresh;
    REQUIRE(fresh.observation_count() == 0);

    const CapturedFace captured = fresh.capture();
    for (const auto& lab : captured.lab) {
        for (int channel = 0; channel < 3; ++channel) {
            REQUIRE(std::isfinite(lab[channel]));
            REQUIRE(lab[channel] == 0.0F);
        }
    }

    // The same hazard after reset(), which is how a restarted scan reaches
    // it in practice.
    FaceAggregator aggregator;
    aggregator.add(observation_with(base_colors()));
    aggregator.reset();
    REQUIRE(aggregator.observation_count() == 0);
    const CapturedFace after_reset = aggregator.capture();
    for (const auto& lab : after_reset.lab) {
        REQUIRE(lab[0] == 0.0F);
    }
}
