// cube_solve_bench: what does solve() cost on a seeded corpus?
//
//   cube_solve_bench [--method beginner|intermediate|advanced]
//                    [--count N] [--seed S] [--moves M] [--list]
//
// Solves the scrambles random_scramble(S), random_scramble(S+1), ... (M moves
// each, 25 by default - the length the solver tests use) and prints the wall
// time per solve and a digest of every solution. The digest covers each
// solution's stages and moves, so two runs that print the same digest produced
// byte-identical solutions; a change that is meant to be a pure speed-up must
// leave it alone.
//
// Timings are wall-clock around cube::solver::solve() only. For where inside
// a solve the time goes, run this under Tracy (docs/profiling.md): the zones
// are the solver's own.
//
// `advanced` pays its lookup-table build on the first solve unless it is done
// ahead of time; the tool does it first, as the app does while a person is
// still scanning, and reports it separately.

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    cube::solver::SolveMethod method = cube::solver::SolveMethod::Beginner;
    unsigned count = 28;
    unsigned seed = 0;
    int moves = 25;
    bool list = false;
};

[[nodiscard]] std::string_view method_name(cube::solver::SolveMethod method) {
    switch (method) {
        case cube::solver::SolveMethod::Beginner:
            return "beginner";
        case cube::solver::SolveMethod::Intermediate:
            return "intermediate";
        case cube::solver::SolveMethod::Advanced:
            return "advanced";
    }
    return "?";
}

[[nodiscard]] bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--list") {
            options.list = true;
        } else if (arg == "--method") {
            const char* name = value();
            if (name == nullptr) {
                return false;
            }
            const std::string_view method = name;
            if (method == "beginner") {
                options.method = cube::solver::SolveMethod::Beginner;
            } else if (method == "intermediate") {
                options.method = cube::solver::SolveMethod::Intermediate;
            } else if (method == "advanced") {
                options.method = cube::solver::SolveMethod::Advanced;
            } else {
                return false;
            }
        } else if (arg == "--count" || arg == "--seed" || arg == "--moves") {
            const char* text = value();
            if (text == nullptr) {
                return false;
            }
            const long number = std::strtol(text, nullptr, 10);
            if (number < 0) {
                return false;
            }
            if (arg == "--count") {
                options.count = static_cast<unsigned>(number);
            } else if (arg == "--seed") {
                options.seed = static_cast<unsigned>(number);
            } else {
                options.moves = static_cast<int>(number);
            }
        } else {
            return false;
        }
    }
    return true;
}

constexpr std::uint64_t kDigestStart = 0xCBF29CE484222325ULL;

// FNV-1a over the notation of every step, with the stage and a separator, so
// moving a move across a stage boundary changes the digest too.
void mix(std::uint64_t& hash, std::string_view text) {
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001B3ULL;
    }
}

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::fputs(
            "usage: cube_solve_bench [--method beginner|intermediate|advanced]"
            " [--count N] [--seed S] [--moves M] [--list]\n",
            stderr);
        return 2;
    }

    if (options.method == cube::solver::SolveMethod::Advanced) {
        const auto start = Clock::now();
        cube::solver::prepare(options.method);
        std::printf("prepare: %.1f ms\n",
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }

    std::vector<double> millis;
    std::uint64_t digest = kDigestStart;
    std::size_t total_moves = 0;
    unsigned failed = 0;

    for (unsigned i = 0; i < options.count; ++i) {
        const unsigned seed = options.seed + i;
        const cube::solver::Cube scrambled = cube::solver::random_scramble(seed, options.moves);

        const auto start = Clock::now();
        const auto solution = cube::solver::solve(scrambled, options.method);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

        if (!solution.has_value()) {
            ++failed;
            mix(digest, "FAILED;");
            std::printf("seed %u: FAILED\n", seed);
            continue;
        }
        millis.push_back(ms);
        std::size_t moves = 0;
        // The same bytes feed the corpus digest and this solution's own.
        std::uint64_t own = kDigestStart;
        const auto both = [&](std::string_view text) {
            mix(digest, text);
            mix(own, text);
        };
        for (const cube::solver::SolveStep& step : solution->steps) {
            moves += step.moves.size();
            both(std::to_string(static_cast<int>(step.stage)));
            both(":");
            both(cube::solver::to_string(step.moves));
            both(";");
        }
        total_moves += moves;
        if (options.list) {
            std::printf("seed %u: %8.1f ms  %3zu moves  %016llx\n", seed, ms, moves,
                        static_cast<unsigned long long>(own));
        }
    }

    if (millis.empty()) {
        std::puts("no solves succeeded");
        return 1;
    }
    double total = 0.0;
    for (const double ms : millis) {
        total += ms;
    }
    const double mean = total / static_cast<double>(millis.size());
    std::printf(
        "%s  seeds %u..%u  %d-move scrambles  %zu solves, %u failed\n"
        "  ms per solve: mean %.1f  p50 %.1f  p95 %.1f  max %.1f  (total %.0f)\n"
        "  moves per solve: mean %.1f\n"
        "  digest: %016llx\n",
        std::string(method_name(options.method)).c_str(), options.seed,
        options.seed + options.count - 1, options.moves, millis.size(), failed, mean,
        percentile(millis, 0.5), percentile(millis, 0.95), percentile(millis, 1.0), total,
        static_cast<double>(total_moves) / static_cast<double>(millis.size()),
        static_cast<unsigned long long>(digest));
    return failed == 0 ? 0 : 1;
}
