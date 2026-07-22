#pragma once

#include <cube/vision/face_observation.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace cube::vision {

// A face's colours agreed across several frames: per-cell robust (median
// per Lab channel) colour, ready for final classification.
struct CapturedFace {
    std::array<cv::Vec3f, 9> lab{};
};

// Accumulates per-frame FaceObservations of one face and reports when the
// per-cell colours have converged - the replacement for single-frame
// capture plus a separate steadiness gate. Motion blur, a passing finger,
// or a half-rotated cube all show up as cross-frame variance, so
// "converged" inherently means "steady, unoccluded, consistent reading"
// without needing a dedicated stability tracker in the capture path.
class FaceAggregator {
public:
    // Number of most-recent observations considered (ring buffer).
    static constexpr std::size_t kWindowSize = 8;
    // Minimum observations before convergence can be reported. At the scan
    // screen's poll rate this makes the fastest possible capture a few
    // hundred milliseconds - fast enough to feel instant, long enough to
    // reject a single bad frame. Four, not five: on marginal faces (washed
    // pastels on a light lattice) detection lands a few times a second at
    // best, and each observation must arrive within the aggregator
    // staleness window - a fifth consecutive-ish hit priced real captures
    // out entirely on the hardest faces.
    static constexpr std::size_t kMinObservations = 4;
    // Cells allowed to stay unstable without blocking convergence. The
    // physical cube's glossy tiles catch a specular highlight that dances
    // on a cell as the cube shifts in the holder's hand; that cell's
    // cross-frame variance never settles, and requiring all nine cells to
    // be stable made capture unreachable until the user steered the
    // reflection off the face. Two cells of slack covers a highlight
    // straddling a lattice line, while occlusion, blur, and mid-rotation
    // faces - which disturb most of the face at once - still hold off
    // capture. An unstable cell still contributes its window median at
    // capture(): its most-supported reading despite the glare.
    static constexpr std::size_t kMaxUnstableCells = 2;

    void add(const FaceObservation& observation);
    void reset();

    [[nodiscard]] std::size_t observation_count() const noexcept;

    // 0..1 progress toward convergence, for UI feedback (a capture ring /
    // fill animation): grows with both observation count and how tight the
    // per-cell colour spread currently is.
    [[nodiscard]] float convergence() const;

    [[nodiscard]] bool converged() const;

    // Per-cell median Lab over the window. Meaningful once
    // observation_count() >= kMinObservations; callers gate on converged().
    // On an empty window it is defined but not meaningful: every cell reads
    // Lab (0,0,0). It used to be undefined behaviour, which is a worse
    // answer to an unmet precondition than a valueless one.
    [[nodiscard]] CapturedFace capture() const;

private:
    std::array<std::vector<cv::Vec3f>, 9> samples_{};
    std::size_t next_slot_ = 0;
    std::size_t count_ = 0;
};

}  // namespace cube::vision
