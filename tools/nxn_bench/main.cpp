// nxn_bench: what the N x N library finds, and what solving costs, per size.
//
//   nxn_bench library [size]        cycle library statistics
//   nxn_bench solve [lo hi [seeds]] solve random scrambles, report move counts
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cube/nxn/commutator.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/solver.hpp>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void report_group(const char* kind, std::size_t index,
                  std::span<const cube::nxn::ThreeCycle> cycles) {
    std::size_t shortest = 1000;
    std::size_t longest = 0;
    double total = 0;
    for (const auto& c : cycles) {
        shortest = std::min(shortest, c.word.size());
        longest = std::max(longest, c.word.size());
        total += static_cast<double>(c.word.size());
    }
    // Ordered pairs (from, to) of cells that some cycle moves directly: a
    // piece can only be put into a cell in one step if its pair is here.
    std::set<std::pair<std::uint16_t, std::uint16_t>> direct;
    std::set<std::uint16_t> cells;
    for (const auto& c : cycles) {
        for (std::size_t k = 0; k < 3; ++k) {
            direct.insert({c.cells.at(k), c.cells.at((k + 1) % 3)});
            cells.insert(c.cells.at(k));
        }
    }
    const std::size_t possible = cells.size() * (cells.size() - 1);
    std::printf(
        "    %-7s orbit %zu: %5zu of 4048 cycles, word length %zu..%zu, mean %.1f, "
        "pairs reached directly %zu of %zu\n",
        kind, index, cycles.size(), shortest, longest,
        cycles.empty() ? 0.0 : total / static_cast<double>(cycles.size()), direct.size(), possible);
}

int library(int lo, int hi) {
    for (int n = lo; n <= hi; ++n) {
        const auto start = Clock::now();
        const auto& geometry = cube::nxn::Geometry::of(n);
        const double geometry_ms = ms_since(start);
        const auto library_start = Clock::now();
        const auto& library = cube::nxn::CycleLibrary::of(n);
        const double library_ms = ms_since(library_start);
        std::printf("N=%d  geometry %.1f ms  cycle library %.1f ms\n", n, geometry_ms, library_ms);
        for (std::size_t k = 0; k < geometry.centre_orbits().size(); ++k) {
            report_group("centre", k, library.centres(k));
        }
        for (std::size_t k = 0; k < geometry.wing_orbits().size(); ++k) {
            report_group("wing", k, library.wings(k));
        }
    }
    return 0;
}

int solve(int lo, int hi, int seeds) {
    std::printf("%4s %6s %9s %9s %9s %9s %9s %9s %9s\n", "N", "ok", "parity", "centres", "edges",
                "core", "total", "max", "ms/solve");
    for (int n = lo; n <= hi; ++n) {
        std::size_t parity = 0;
        std::size_t centres = 0;
        std::size_t edges = 0;
        std::size_t core = 0;
        std::size_t total = 0;
        std::size_t longest = 0;
        int ok = 0;
        double elapsed = 0;
        for (int seed = 1; seed <= seeds; ++seed) {
            const auto cube = cube::nxn::random_scramble(
                {.size = n, .seed = static_cast<unsigned>(seed), .moves = 60 + (10 * n)});
            const auto start = Clock::now();
            const auto solution = cube::nxn::solve(cube);
            elapsed += ms_since(start);
            if (!solution) {
                std::printf("  N=%d seed %d: %s\n", n, seed,
                            std::string(cube::to_string(solution.error())).c_str());
                continue;
            }
            ++ok;
            for (const auto& step : solution->steps) {
                using cube::nxn::Stage;
                std::size_t& bucket = step.stage == Stage::Parity    ? parity
                                      : step.stage == Stage::Centres ? centres
                                      : step.stage == Stage::Edges   ? edges
                                                                     : core;
                bucket += step.moves.size();
            }
            total += solution->move_count();
            longest = std::max(longest, solution->move_count());
        }
        const double d = ok > 0 ? static_cast<double>(ok) : 1.0;
        std::printf("%4d %3d/%-2d %9.1f %9.1f %9.1f %9.1f %9.1f %9zu %9.1f\n", n, ok, seeds,
                    static_cast<double>(parity) / d, static_cast<double>(centres) / d,
                    static_cast<double>(edges) / d, static_cast<double>(core) / d,
                    static_cast<double>(total) / d, longest, elapsed / static_cast<double>(seeds));
    }
    return 0;
}

// Move counts of the three 3x3 methods over many scrambles: what the core of
// every size costs, and whether it is bounded.
int core(int seeds) {
    std::printf("%-14s %6s %6s %6s %6s %8s\n", "method", "min", "mean", "p99", "max", "ms/solve");
    const std::array<std::pair<const char*, cube::solver::SolveMethod>, 3> methods = {{
        {"beginner", cube::solver::SolveMethod::Beginner},
        {"intermediate", cube::solver::SolveMethod::Intermediate},
        {"advanced", cube::solver::SolveMethod::Advanced},
    }};
    for (const auto& [name, method] : methods) {
        std::vector<std::size_t> counts;
        double elapsed = 0;
        for (int seed = 1; seed <= seeds; ++seed) {
            const auto cube = cube::solver::random_scramble(static_cast<unsigned>(seed),
                                                            cube::solver::MoveCount{40});
            const auto start = Clock::now();
            const auto solution = cube::solver::solve(cube, method);
            elapsed += ms_since(start);
            if (!solution) {
                continue;
            }
            std::size_t total = 0;
            for (const auto& step : solution->steps) {
                total += step.moves.size();
            }
            counts.push_back(total);
        }
        std::ranges::sort(counts);
        const double mean =
            static_cast<double>(std::accumulate(counts.begin(), counts.end(), std::size_t{0})) /
            static_cast<double>(counts.size());
        std::printf("%-14s %6zu %6.1f %6zu %6zu %8.1f\n", name, counts.front(), mean,
                    counts.at(counts.size() * 99 / 100), counts.back(),
                    elapsed / static_cast<double>(seeds));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "solve";
    const int lo = argc > 2 ? std::atoi(argv[2]) : 2;
    const int hi = argc > 3 ? std::atoi(argv[3]) : (argc > 2 ? lo : 7);
    const int seeds = argc > 4 ? std::atoi(argv[4]) : 10;
    if (mode == "core") {
        return core(argc > 2 ? lo : 500);
    }
    if (mode == "library") {
        return library(argc > 2 ? lo : 4, argc > 2 ? hi : 7);
    }
    return solve(lo, hi, seeds);
}
