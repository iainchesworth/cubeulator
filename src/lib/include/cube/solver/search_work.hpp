#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cube::solver {

// What the solver's searches have cost so far on this thread, counted in
// nodes: the states a search looked at, which is the one figure that does not
// depend on the machine, the compiler or the build type. The first-two-layers
// searches are where a solve's time goes, and their node counts are why a
// change that loses a signature reduction costs seconds long before anyone
// notices it in a profile.
//
// Written by the searches themselves (piece_search and bfs_search, the
// last-layer search, the CFOP pair search and the two-phase search), once per
// search as it ends, so it costs a thread-local add per search and nothing per
// node. The performance tests (tests/perf, docs/testing.md "Performance
// tests") reset it, make a call and read it; nothing in the application does.
struct SearchWork {
    // Searches that ran far enough to visit a node. A search whose start
    // already met its goal is not one.
    std::uint64_t searches = 0;
    // Nodes visited by all of them.
    std::uint64_t nodes = 0;
    // The most nodes any one of them visited.
    std::uint64_t largest = 0;
};

namespace detail {

// One tally per thread. A function's static and not a namespace-scope variable:
// the same thing, without being a non-const global that anything can reach.
[[nodiscard]] inline SearchWork& tally() noexcept {
    static thread_local SearchWork work;
    return work;
}

inline void note_search(std::uint64_t nodes) noexcept {
    SearchWork& work = tally();
    ++work.searches;
    work.nodes += nodes;
    work.largest = std::max(work.largest, nodes);
}

// Reports a search's node count when the scope it is in ends, by whichever
// return it takes: a search has several, and one that gives up at its cap is
// the case most worth seeing. `count` is called once, at the end.
template <typename Count>
class SearchWorkScope {
public:
    explicit SearchWorkScope(Count count) : count_(std::move(count)) {}
    ~SearchWorkScope() { note_search(static_cast<std::uint64_t>(count_())); }

    SearchWorkScope(const SearchWorkScope&) = delete;
    SearchWorkScope& operator=(const SearchWorkScope&) = delete;
    SearchWorkScope(SearchWorkScope&&) = delete;
    SearchWorkScope& operator=(SearchWorkScope&&) = delete;

private:
    Count count_;
};

}  // namespace detail

// This thread's running total since the last reset.
[[nodiscard]] inline SearchWork search_work() noexcept {
    return detail::tally();
}

// Starts this thread's total from zero: reset, make the call, read. (Not a
// before-and-after subtraction, because `largest` is a maximum.)
inline void reset_search_work() noexcept {
    detail::tally() = {};
}

}  // namespace cube::solver
