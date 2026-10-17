#include <catch2/catch_test_macros.hpp>

#include <cube/vision/read_set.hpp>

#include "deterministic_rng.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using cube::vision::ReadSet;
using cube::vision::RunningMedian;

namespace {

// What the scan did before it kept a running median: sort a copy of every value
// there was, at every read (std::nth_element, then the mean of the two in the
// middle when there is an even number).
float median_by_sorting(std::vector<float> values) {
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    if (values.size() % 2 == 1) {
        return *mid;
    }
    const float lower = *std::max_element(values.begin(), mid);
    return (lower + *mid) / 2.0F;
}

using Read = std::array<cv::Vec3f, 9>;

Read random_read(std::mt19937& rng, bool coarse) {
    // `coarse` draws from a few values, so that ties are most of what there is.
    std::uniform_int_distribution<int> steps(0, 6);
    std::uniform_real_distribution<float> fine(0.0F, 100.0F);
    Read read{};
    for (cv::Vec3f& cell : read) {
        for (int channel = 0; channel < 3; ++channel) {
            cell(channel) = coarse ? static_cast<float>(steps(rng)) * 0.5F : fine(rng);
        }
    }
    return read;
}

// Every cell and channel of `set`'s median is the median, by sorting, of that
// cell and channel of `reads`, to the bit.
void require_median_of(const ReadSet& set, const std::vector<Read>& reads) {
    REQUIRE(set.size() == reads.size());
    const std::vector<cv::Vec3f> fused = set.median();
    for (std::size_t cell = 0; cell < 9; ++cell) {
        for (int channel = 0; channel < 3; ++channel) {
            std::vector<float> values;
            values.reserve(reads.size());
            for (const Read& read : reads) {
                values.push_back(read.at(cell)(channel));
            }
            REQUIRE(fused.at(cell)(channel) == median_by_sorting(values));
        }
    }
}

// `read` as it lies once cell i has taken cell source[i].
Read turned(const Read& read, const std::array<int, 9>& source) {
    Read out{};
    for (std::size_t cell = 0; cell < 9; ++cell) {
        out.at(cell) = read.at(static_cast<std::size_t>(source.at(cell)));
    }
    return out;
}

}  // namespace

TEST_CASE("a running median is the median of every value so far, to the bit", "[read-set]") {
    for (const bool coarse : {false, true}) {
        std::mt19937 rng = cube::test::deterministic_rng(coarse ? 11U : 12U);
        std::uniform_int_distribution<int> steps(0, 6);
        std::uniform_real_distribution<float> fine(-50.0F, 50.0F);
        RunningMedian running;
        std::vector<float> seen;
        seen.reserve(600);
        for (int i = 0; i < 600; ++i) {
            const float value = coarse ? static_cast<float>(steps(rng)) * 0.25F : fine(rng);
            running.add(value);
            seen.push_back(value);
            REQUIRE(running.size() == seen.size());
            // The same bits, not nearly: the value is one of the inputs or the
            // sum of two of them halved, computed the same way.
            REQUIRE(running.median() == median_by_sorting(seen));
        }
    }
}

TEST_CASE("a read set is the per-cell median of its reads", "[read-set]") {
    std::mt19937 rng = cube::test::deterministic_rng(21U);
    for (const bool coarse : {false, true}) {
        ReadSet set;
        std::vector<Read> reads;
        reads.reserve(120);
        for (int i = 0; i < 120; ++i) {
            reads.push_back(random_read(rng, coarse));
            set.add(reads.back());
            require_median_of(set, reads);
        }
    }
}

TEST_CASE("a read set absorbs another's reads turned under a quarter-turn", "[read-set]") {
    // The permutations direct_scan lays one grouping of a side on another by: the
    // cell of the other's read that lands at cell i.
    const std::array<std::array<int, 9>, 4> turns = {
        {
            {0, 1, 2, 3, 4, 5, 6, 7, 8},
            {2, 5, 8, 1, 4, 7, 0, 3, 6},
            {8, 7, 6, 5, 4, 3, 2, 1, 0},
            {6, 3, 0, 7, 4, 1, 8, 5, 2},
        },
    };
    std::mt19937 rng = cube::test::deterministic_rng(31U);
    for (int roll = 0; roll < 4; ++roll) {
        const std::array<int, 9>& turn = turns.at(static_cast<std::size_t>(roll));
        ReadSet kept;
        ReadSet other;
        std::vector<Read> all;
        all.reserve(96);
        for (int i = 0; i < 40; ++i) {
            all.push_back(random_read(rng, i % 2 == 0));
            kept.add(all.back());
        }
        for (int i = 0; i < 55; ++i) {
            const Read read = random_read(rng, i % 3 == 0);
            other.add(read);
            all.push_back(turned(read, turn));
        }
        kept.absorb(other, roll);
        require_median_of(kept, all);

        // And what is added after is added to the same set.
        all.push_back(random_read(rng, false));
        kept.add(all.back());
        require_median_of(kept, all);
    }
}
