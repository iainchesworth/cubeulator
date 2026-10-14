#include "two_by_two.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace cube::nxn::detail {

namespace {

// A 2x2's 24 stickers as bytes: cheap to hash and compare.
using Key = std::string;

constexpr int kMaxDepth = 14;  // 11 is God's number in half turns; margin for the join

constexpr std::array<LayerMove, 9> kMoves = {{
    {.face = Face::U, .depth = 0, .turns = 1},
    {.face = Face::U, .depth = 0, .turns = 2},
    {.face = Face::U, .depth = 0, .turns = 3},
    {.face = Face::R, .depth = 0, .turns = 1},
    {.face = Face::R, .depth = 0, .turns = 2},
    {.face = Face::R, .depth = 0, .turns = 3},
    {.face = Face::F, .depth = 0, .turns = 1},
    {.face = Face::F, .depth = 0, .turns = 2},
    {.face = Face::F, .depth = 0, .turns = 3},
}};

[[nodiscard]] Key key_of(const NCube& cube) {
    Key key;
    key.reserve(cube.facelets().size());
    for (const Color colour : cube.facelets()) {
        key.push_back(static_cast<char>(colour));
    }
    return key;
}

[[nodiscard]] NCube cube_of(const Geometry& geometry, const Key& key) {
    std::vector<Color> facelets;
    facelets.reserve(key.size());
    for (const char c : key) {
        facelets.push_back(static_cast<Color>(c));
    }
    // A key is always one of the 24 stickers of a valid colouring.
    return *NCube::from_facelets(geometry.size(), facelets);
}

// How a state was reached: the state before it and the turn made.
struct Reached {
    Key parent;
    std::int8_t move = -1;  // index into kMoves, -1 at the root
};

// One end of the search: every state seen from it, and the newest of them.
struct Frontier {
    std::unordered_map<Key, Reached> seen;
    std::vector<Key> edge;
};

// Turns every state on `side`'s edge by every move. Returns the first state
// that `other` has seen too, if any.
[[nodiscard]] std::optional<Key> expand(const Geometry& geometry, Frontier& side,
                                        const Frontier& other) {
    std::vector<Key> next;
    for (const Key& key : side.edge) {
        const NCube base = cube_of(geometry, key);
        for (std::size_t m = 0; m < kMoves.size(); ++m) {
            NCube moved = base;
            moved.apply(kMoves.at(m));
            Key moved_key = key_of(moved);
            const auto [at, fresh] = side.seen.try_emplace(
                moved_key, Reached{.parent = key, .move = static_cast<std::int8_t>(m)});
            if (!fresh) {
                continue;
            }
            if (other.seen.contains(moved_key)) {
                return moved_key;
            }
            next.push_back(std::move(moved_key));
        }
    }
    side.edge = std::move(next);
    return std::nullopt;
}

// The moves from `start` (the root of `side`) to `at`, in order.
[[nodiscard]] std::vector<LayerMove> path_to(const Frontier& side, Key at) {
    std::vector<LayerMove> path;
    while (side.seen.at(at).move >= 0) {
        const Reached& reached = side.seen.at(at);
        path.push_back(kMoves.at(static_cast<std::size_t>(reached.move)));
        at = reached.parent;
    }
    std::ranges::reverse(path);
    return path;
}

// The moves from `at` back to the root of `side`, each undone: `side` was
// grown from the goal by the same moves, so walking it from the meeting
// point to the goal undoes each of them.
[[nodiscard]] std::vector<LayerMove> path_from(const Frontier& side, Key at) {
    std::vector<LayerMove> path;
    while (side.seen.at(at).move >= 0) {
        const Reached& reached = side.seen.at(at);
        path.push_back(kMoves.at(static_cast<std::size_t>(reached.move)).inverse());
        at = reached.parent;
    }
    return path;
}

}  // namespace

Result<std::vector<LayerMove>> solve_two_by_two(const NCube& scrambled) {
    const Geometry& geometry = scrambled.geometry();
    const auto scheme = scheme_of(scrambled);
    if (!scheme) {
        return std::unexpected(scheme.error());
    }
    std::vector<Color> goal_facelets(geometry.facelet_count());
    for (std::size_t i = 0; i < goal_facelets.size(); ++i) {
        goal_facelets.at(i) = scheme->at(static_cast<std::size_t>(geometry.face_of(i)));
    }
    const auto goal = NCube::from_facelets(2, goal_facelets);
    if (!goal) {
        return std::unexpected(CubeError::invalid_cube_state);
    }

    const Key start_key = key_of(scrambled);
    const Key goal_key = key_of(*goal);
    if (start_key == goal_key) {
        return std::vector<LayerMove>{};
    }

    Frontier forward;
    Frontier backward;
    forward.seen.emplace(start_key, Reached{});
    backward.seen.emplace(goal_key, Reached{});
    forward.edge = {start_key};
    backward.edge = {goal_key};

    std::optional<Key> meeting;
    for (int depth = 0; depth < kMaxDepth && !meeting; ++depth) {
        if (forward.edge.empty() && backward.edge.empty()) {
            break;
        }
        // Grow the smaller side: both ends stay about the same size.
        meeting = forward.edge.size() <= backward.edge.size() ? expand(geometry, forward, backward)
                                                              : expand(geometry, backward, forward);
    }
    if (!meeting) {
        return std::unexpected(CubeError::unsolvable_state);
    }

    // start ... meeting, then meeting ... goal.
    std::vector<LayerMove> path = path_to(forward, *meeting);
    const std::vector<LayerMove> rest = path_from(backward, *meeting);
    path.insert(path.end(), rest.begin(), rest.end());
    return path;
}

}  // namespace cube::nxn::detail
