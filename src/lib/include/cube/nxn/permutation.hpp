#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

namespace cube::nxn {

// A rearrangement of facelets, in the "gather" convention: after the
// permutation `p` is applied to a state `s`, the result is `r` with
// r[i] = s[p[i]] - the sticker that shows at i afterwards is the one that
// showed at p[i] before. A permutation is a vector of facelet indices, so one
// type serves for a layer turn, a whole-cube rotation and a sequence of turns
// that has been folded into one.
using Permutation = std::vector<std::uint16_t>;

[[nodiscard]] inline Permutation identity_permutation(std::size_t size) {
    Permutation p(size);
    std::ranges::copy(std::views::iota(std::uint16_t{0}, static_cast<std::uint16_t>(size)),
                      p.begin());
    return p;
}

// `first` followed by `second`. Associative; identity_permutation() is its
// unit. result[i] = first[second[i]] follows from the gather convention: the
// sticker at i after both is the one `second` took from j = second[i], which
// is the one `first` took from first[j].
[[nodiscard]] inline Permutation then(std::span<const std::uint16_t> first,
                                      std::span<const std::uint16_t> second) {
    Permutation result(second.size());
    std::ranges::transform(second, result.begin(), [&first](std::uint16_t j) { return first[j]; });
    return result;
}

[[nodiscard]] inline Permutation inverse(std::span<const std::uint16_t> p) {
    Permutation result(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        result[p[i]] = static_cast<std::uint16_t>(i);
    }
    return result;
}

// The facelets the permutation moves, in ascending order.
[[nodiscard]] inline std::vector<std::uint16_t> support(std::span<const std::uint16_t> p) {
    std::vector<std::uint16_t> moved;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] != i) {
            moved.push_back(static_cast<std::uint16_t>(i));
        }
    }
    return moved;
}

// +1 for an even permutation, -1 for an odd one: the parity of the number of
// transpositions that build it, found from its cycle structure.
[[nodiscard]] inline int sign(std::span<const std::uint16_t> p) {
    std::vector<bool> seen(p.size(), false);
    int parity = 0;
    for (std::size_t start = 0; start < p.size(); ++start) {
        if (seen[start]) {
            continue;
        }
        std::size_t length = 0;
        for (std::size_t at = start; !seen[at]; at = p[at]) {
            seen[at] = true;
            ++length;
        }
        parity += static_cast<int>(length - 1);
    }
    return (parity % 2 == 0) ? 1 : -1;
}

}  // namespace cube::nxn
