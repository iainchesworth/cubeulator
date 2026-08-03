#pragma once

// Per-frame observability contract of the pipeline (docs/pipeline-plan.md:
// "Trace everything, enforced"). Every stage appends exactly one
// StageTrace per frame — including stages that did not run, which record
// ran=false and a reason — and the runner's self-check FAILS any frame
// whose trace is incomplete. Discreteness is a test, not an aspiration.
//
// Serialisation is JSON Lines: one frame per line, schema documented in
// docs/pipeline-trace-schema.md and pinned by pipeline_scaffold_test. The
// writer is deterministic by construction (std::to_chars for numbers, no
// locale involvement, insertion-ordered fields) so byte-identical traces
// are the replay-determinism check.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cube::vision::pipeline {

// Canonical stage names, in pipeline order. A FrameTrace is complete iff
// it carries exactly these, in this order.
inline constexpr std::array<std::string_view, 9> kStageNames{
    "acquire", "detect", "track", "faces", "cells", "sample", "classify", "fuse", "assemble",
};

using TraceValue = std::variant<bool, std::int64_t, double, std::string>;

struct StageTrace {
    std::string stage;
    bool ran = false;
    double duration_ms = 0.0;
    // Insertion-ordered flat payload. Keys are stable schema; see
    // docs/pipeline-trace-schema.md before renaming anything.
    std::vector<std::pair<std::string, TraceValue>> fields;

    void add(std::string key, TraceValue value) { fields.emplace_back(std::move(key), std::move(value)); }
};

struct FrameTrace {
    int seq = 0;
    std::int64_t t_ms = 0;
    std::vector<StageTrace> stages;
};

// One JSON Lines record (single line, no trailing newline).
[[nodiscard]] std::string to_jsonl(const FrameTrace& trace);

// Completeness check: exactly the canonical stages, in order, each named.
// Returns the first problem as a human-readable message, or nullopt when
// the trace is complete. The runner and the scaffold test both fail on a
// non-empty result — this is the enforced half of "trace everything".
[[nodiscard]] std::optional<std::string> validate_trace(const FrameTrace& trace);

}  // namespace cube::vision::pipeline
