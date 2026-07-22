#include <cube/vision/temporal_fusion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace cube::vision {

namespace {

// Windows hold at most kWindowSize samples, so a partial sort per query is
// negligible; values are taken by value so the caller's ring buffers keep
// their eviction order.
[[nodiscard]] float median(std::vector<float> values) {
    // An empty window has no median. Every line below assumes at least one
    // value: `values[0]` on an empty vector dereferences a null data()
    // pointer, and the even-size branch then dereferences the end iterator
    // std::max_element returns for an empty range. Reached whenever
    // capture() is called before any add() or after reset() - a public,
    // unguarded entry point whose documented precondition ("callers gate on
    // converged()") only production callers happen to honour.
    if (values.empty()) {
        return 0.0F;
    }
    const auto mid = static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const float upper = values[static_cast<std::size_t>(mid)];
    if (values.size() % 2 != 0) {
        return upper;
    }
    const float lower = *std::max_element(values.begin(), values.begin() + mid);
    return (lower + upper) * 0.5F;
}

[[nodiscard]] float channel_median(const std::vector<cv::Vec3f>& samples, int channel) {
    std::vector<float> values;
    values.reserve(samples.size());
    for (const auto& lab : samples) {
        values.push_back(lab[channel]);
    }
    return median(std::move(values));
}

// Median absolute deviation rather than standard deviation: one occluded or
// blurred frame in the window should read as an outlier, not inflate the
// spread estimate enough to stall convergence long after the bad frame has
// been evicted.
[[nodiscard]] float channel_mad(const std::vector<cv::Vec3f>& samples, int channel) {
    const float center = channel_median(samples, channel);
    std::vector<float> deviations;
    deviations.reserve(samples.size());
    for (const auto& lab : samples) {
        deviations.push_back(std::abs(lab[channel] - center));
    }
    return median(std::move(deviations));
}

// A cell is only as settled as its noisiest Lab channel: per-channel MADs
// collapse to one per-cell spread before cells are ranked against each
// other.
[[nodiscard]] float cell_mad(const std::vector<cv::Vec3f>& samples) {
    float worst = 0.0F;
    for (int channel = 0; channel < 3; ++channel) {
        worst = std::max(worst, channel_mad(samples, channel));
    }
    return worst;
}

// Cells the convergence gate must see individually stable.
constexpr std::size_t kRequiredStableCells = 9 - FaceAggregator::kMaxUnstableCells;

// The gate used to be the *worst* cell's spread, so that a fingertip
// covering a single sticker held off capture - but a specular highlight
// parked on one glossy tile keeps that cell's variance high indefinitely,
// and capture became unreachable on the physical cube (observed live).
// Ranking cells by spread and gating on the kRequiredStableCells-th best
// tolerates up to kMaxUnstableCells glare-struck cells: the returned value
// is under the accept threshold exactly when at least kRequiredStableCells
// cells are individually under it. Occlusion, blur, and mid-rotation faces
// disturb more cells than the allowance and stay blocked.
[[nodiscard]] float gate_mad(const std::array<std::vector<cv::Vec3f>, 9>& cells) {
    std::array<float, 9> spreads{};
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        spreads[cell] = cell_mad(cells[cell]);
    }
    const auto nth = spreads.begin() + static_cast<std::ptrdiff_t>(kRequiredStableCells - 1);
    std::nth_element(spreads.begin(), nth, spreads.end());
    return *nth;
}

// Accept threshold in Lab units. A steady, unoccluded sticker read through
// patch medians varies by a couple of units frame to frame; motion blur,
// occlusion, or a mid-rotation face pushes well more than kMaxUnstableCells
// cells past this.
constexpr float kMaxConvergedMad = 6.0F;

// Spread at which convergence() bottoms out - twice the accept threshold,
// so the UI's progress indicator starts filling while the reading is merely
// "getting close" instead of only once it is already acceptable.
constexpr float kMadForZeroProgress = 12.0F;

}  // namespace

void FaceAggregator::add(const FaceObservation& observation) {
    for (std::size_t cell = 0; cell < observation.lab.size(); ++cell) {
        auto& samples = samples_[cell];
        if (samples.size() < kWindowSize) {
            samples.push_back(observation.lab[cell]);
        } else {
            samples[next_slot_] = observation.lab[cell];
        }
    }
    next_slot_ = (next_slot_ + 1) % kWindowSize;
    count_ = std::min(count_ + 1, kWindowSize);
}

void FaceAggregator::reset() {
    for (auto& samples : samples_) {
        samples.clear();
    }
    next_slot_ = 0;
    count_ = 0;
}

std::size_t FaceAggregator::observation_count() const noexcept {
    return count_;
}

float FaceAggregator::convergence() const {
    if (count_ == 0) {
        return 0.0F;
    }
    // Product of two monotone factors: fill (observation count toward the
    // minimum) and tightness (linear ramp from kMadForZeroProgress down to a
    // perfectly steady reading). Tightness ramps on the same Nth-best cell
    // spread that converged() thresholds, so the progress ring can reach
    // full even while a tolerated glare cell is still dancing. Multiplying
    // keeps a single frame - which is trivially self-consistent, MAD 0 -
    // from reporting full progress, and makes the value hit 1 exactly when
    // both "enough frames" and "steady" hold with room to spare.
    const float fill =
        std::min(static_cast<float>(count_) / static_cast<float>(kMinObservations), 1.0F);
    const float tightness = std::clamp(1.0F - gate_mad(samples_) / kMadForZeroProgress, 0.0F, 1.0F);
    return fill * tightness;
}

bool FaceAggregator::converged() const {
    return count_ >= kMinObservations && gate_mad(samples_) < kMaxConvergedMad;
}

CapturedFace FaceAggregator::capture() const {
    CapturedFace face;
    for (std::size_t cell = 0; cell < face.lab.size(); ++cell) {
        for (int channel = 0; channel < 3; ++channel) {
            face.lab[cell][channel] = channel_median(samples_[cell], channel);
        }
    }
    return face;
}

}  // namespace cube::vision
