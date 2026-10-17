#pragma once

#include <cube/vision/side_grid.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <span>
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

// The reads of one side (N x N Lab triples each) as what is taken of them: the
// median of each channel of each cell, which is the side's fused colours. Not
// the reads themselves - nothing asks for one again - so a read costs a
// heap insertion for each of its 3 N^2 numbers, not a sort of all of them, and
// a side held in view for a minute is not slower to read at the end of it than
// at the start.
//
// The size is the size of the first read added; every read of one set is of one.
class ReadSet {
public:
    void add(std::span<const cv::Vec3f> read) {
        if (series_.empty()) {
            size_ = side_length_of(read.size());
            series_.resize(read.size() * 3);
        }
        std::size_t cell = 0;
        for (const cv::Vec3f& value : read) {
            for (int channel = 0; channel < 3; ++channel) {
                series_.at((cell * 3) + static_cast<std::size_t>(channel)).add(value(channel));
            }
            ++cell;
        }
        ++count_;
    }

    // Every read of `other`, each turned `roll` quarter-turns so that its cell
    // rolled_cell(size, roll, i) lies at cell i (the turn that lays one
    // grouping of a side on another).
    void absorb(const ReadSet& other, int roll) {
        if (other.series_.empty()) {
            return;
        }
        if (series_.empty()) {
            size_ = other.size_;
            series_.resize(other.series_.size());
        }
        for (std::size_t cell = 0; cell < series_.size() / 3; ++cell) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto from = (rolled_cell(size_, roll, cell) * 3) + channel;
                RunningMedian& into = series_.at((cell * 3) + channel);
                other.series_.at(from).for_each([&into](float value) { into.add(value); });
            }
        }
        count_ += other.count_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    // Of at least one read.
    [[nodiscard]] std::vector<cv::Vec3f> median() const {
        std::vector<cv::Vec3f> fused(series_.size() / 3);
        for (std::size_t cell = 0; cell < fused.size(); ++cell) {
            for (int channel = 0; channel < 3; ++channel) {
                fused.at(cell)(channel) =
                    series_.at((cell * 3) + static_cast<std::size_t>(channel)).median();
            }
        }
        return fused;
    }

private:
    std::size_t count_ = 0;
    int size_ = 0;
    std::vector<RunningMedian> series_;
};

}  // namespace cube::vision
