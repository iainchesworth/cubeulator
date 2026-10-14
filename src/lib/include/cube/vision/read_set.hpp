#pragma once

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace cube::vision {

// The median of a set of numbers that only grows, answered without sorting it
// again: two heaps, the lower half under its largest and the upper half over its
// smallest, so that adding a value costs a heap insertion and the median is one
// or two values at the tops.
//
// It is the median `SideScan` always took (the middle value of an odd count, and
// for an even one the mean of the two in the middle, as `(lower + upper) / 2` in
// float), to the bit: the same values are compared and the same two are added.
class RunningMedian {
public:
    void add(float value) {
        if (low_.empty() || value <= low_.front()) {
            low_.push_back(value);
            std::ranges::push_heap(low_, std::less<>{});
        } else {
            high_.push_back(value);
            std::ranges::push_heap(high_, std::greater<>{});
        }
        // The lower half has the odd one: the same count as the upper, or one more.
        if (low_.size() > high_.size() + 1) {
            std::ranges::pop_heap(low_, std::less<>{});
            high_.push_back(low_.back());
            low_.pop_back();
            std::ranges::push_heap(high_, std::greater<>{});
        } else if (high_.size() > low_.size()) {
            std::ranges::pop_heap(high_, std::greater<>{});
            low_.push_back(high_.back());
            high_.pop_back();
            std::ranges::push_heap(low_, std::less<>{});
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return low_.size() + high_.size(); }

    // Of at least one value.
    [[nodiscard]] float median() const {
        if (low_.size() > high_.size()) {
            return low_.front();
        }
        return (low_.front() + high_.front()) / 2.0F;
    }

    // Every value, in no order.
    template <typename Visit>
    void for_each(const Visit& visit) const {
        for (const float value : low_) {
            visit(value);
        }
        for (const float value : high_) {
            visit(value);
        }
    }

private:
    std::vector<float> low_;   // a max-heap
    std::vector<float> high_;  // a min-heap
};

// The reads of one side (nine Lab triples each) as what is taken of them: the
// median of each channel of each cell, which is the side's fused colours. Not
// the reads themselves - nothing asks for one again - so a read costs a
// heap insertion for each of its 27 numbers, not a sort of all of them, and a
// side held in view for a minute is not slower to read at the end of it than at
// the start.
class ReadSet {
public:
    void add(const std::array<cv::Vec3f, 9>& read) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            for (int channel = 0; channel < 3; ++channel) {
                series_.at((cell * 3) + static_cast<std::size_t>(channel))
                    .add(read.at(cell)(channel));
            }
        }
        ++count_;
    }

    // Every read of `other`, each turned so that its cell `source[i]` lies at
    // cell i (the quarter-turn that lays one grouping of a side on another).
    void absorb(const ReadSet& other, const std::array<int, 9>& source) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto from = (static_cast<std::size_t>(source.at(cell)) * 3) + channel;
                RunningMedian& into = series_.at((cell * 3) + channel);
                other.series_.at(from).for_each([&into](float value) { into.add(value); });
            }
        }
        count_ += other.count_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    // Of at least one read.
    [[nodiscard]] std::array<cv::Vec3f, 9> median() const {
        std::array<cv::Vec3f, 9> fused{};
        for (std::size_t cell = 0; cell < 9; ++cell) {
            for (int channel = 0; channel < 3; ++channel) {
                fused.at(cell)(channel) =
                    series_.at((cell * 3) + static_cast<std::size_t>(channel)).median();
            }
        }
        return fused;
    }

private:
    std::size_t count_ = 0;
    std::array<RunningMedian, 27> series_;
};

}  // namespace cube::vision
