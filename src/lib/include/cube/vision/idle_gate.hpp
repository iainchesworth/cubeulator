#pragma once

#include <opencv2/core.hpp>

namespace cube::vision {

// ---------------------------------------------------------------------------
// Cube-absent idle gate for the analysis worker's detector cascade.
//
// Live1 measured ~28% of analysis passes cube-absent (the user resting or
// reading the screen), every one burning the full observe_face /
// observe_corner cost on an empty frame. The reclaimed budget is what
// funds the edge-view and low-light detector work without growing the
// worker's p50 pass time - the phone constraint's budget ledger.
//
// The presence signal is a COOL-HUE pixel count on a fixed 160x120
// downsample: a matte tile cube always shows green/blue/cyan tiles at
// strong saturation, while the cube-absent scenes that fooled looser
// gates (face, forehead, warm curtain, wooden desk) are warm-hued or
// unsaturated. Measured over the full live1 recording ([.cool-hue-scan]
// in idle_gate_test.cpp) the statistic is cleanly bimodal - cube-absent
// p90 = 6 (boundary frames where the cube is entering/leaving account
// for every higher value) vs cube-present p05 = 103 - so the floor of 20
// sits in an empty band with wide margin on both sides.
//
// Threading contract: this class is a plain state machine owned by the
// GUI thread and mutated once per completed pass (handleAnalysisFinished),
// exactly like the tracker; the worker only COMPUTES the per-frame count
// (count_cool_hue_pixels, stateless) and receives the skip verdict by
// value at queue time. The one-pass verdict lag that sampling scheme
// implies is covered by the entry hysteresis below.
// ---------------------------------------------------------------------------

// Pixels with H in [35, 135] (OpenCV halved hue: green through blue),
// S >= 60, V >= 40 on a 160x120 INTER_AREA downsample of the frame.
// Bounded work regardless of input resolution; safe on any thread.
[[nodiscard]] int count_cool_hue_pixels(const cv::Mat& bgr_frame);

class IdleGate {
public:
    // Cool-hue floor separating "cube somewhere in frame" from "no cube"
    // (see the bimodal measurement above).
    static constexpr int kIdlePixelFloor = 20;
    // Passes below the floor (while the tracker is lost) before the gate
    // engages: a single dark/blurred frame mid-manipulation must not idle
    // the detectors.
    static constexpr int kIdleConsecutive = 3;
    // Every this-many-th idle pass runs the detectors anyway - the escape
    // hatch bounding worst-case reacquisition latency (~0.4 s at the
    // measured cadence) if the cool-hue statistic ever miscalibrates
    // against a scene.
    static constexpr int kIdleEscapeEvery = 4;

    // Verdict for the NEXT queued pass. True only while the gate is
    // engaged AND the next pass is not an escape pass.
    [[nodiscard]] bool skip_next() const noexcept { return skip_next_; }

    // Exactly one call per completed analysis pass (idle-skipped passes
    // included - the count is computed every pass precisely so cube
    // arrival clears the gate without waiting for an escape pass).
    // tracker_lost is the tracker's post-pass state: the gate may only
    // ever idle a LOST tracker - locked/coasting/acquiring passes carry
    // tracking value however empty the frame looks.
    void on_pass(int cool_hue_pixels, bool tracker_lost) noexcept {
        if (cool_hue_pixels >= kIdlePixelFloor) {
            low_streak_ = 0;
        } else if (low_streak_ < kIdleConsecutive) {  // saturating: no overflow on long absences
            ++low_streak_;
        }
        if (!tracker_lost || low_streak_ < kIdleConsecutive) {
            idle_span_passes_ = 0;
            skip_next_ = false;
            return;
        }
        ++idle_span_passes_;
        skip_next_ = (idle_span_passes_ % kIdleEscapeEvery) != 0;
    }

    void reset() noexcept {
        low_streak_ = 0;
        idle_span_passes_ = 0;
        skip_next_ = false;
    }

private:
    int low_streak_ = 0;
    int idle_span_passes_ = 0;
    bool skip_next_ = false;
};

}  // namespace cube::vision
