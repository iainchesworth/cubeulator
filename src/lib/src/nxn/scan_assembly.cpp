#include <cube/nxn/scan_assembly.hpp>

#include <cube/nxn/validate.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/search_work.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cube::nxn {

namespace {

// The cube position a centre colour pins a side to: the inverse of
// canonical_color(), which is the scheme the 3x3 assembly pins by as well.
constexpr Face face_for_centre(Color colour) noexcept {
    switch (colour) {
        case Color::Yellow:
            return Face::U;
        case Color::White:
            return Face::D;
        case Color::Red:
            return Face::L;
        case Color::Orange:
            return Face::R;
        case Color::Green:
            return Face::F;
        case Color::Blue:
            return Face::B;
    }
    return Face::U;
}

// The three pairs of opposite colours, which no edge or corner carries two of.
constexpr int axis_pair_of(Color colour) noexcept {
    switch (colour) {
        case Color::White:
        case Color::Yellow:
            return 0;
        case Color::Orange:
        case Color::Red:
            return 1;
        case Color::Green:
        case Color::Blue:
            return 2;
    }
    return 0;
}

// The stickers that share a piece and so cannot show two colours of one axis
// pair: the pairs on an edge cubie (a wing, or the middle of an edge) and the
// triples on a corner. Taken from the geometry, so that it is the cube's own
// layout and not a second copy of it; each is filed under the lowest face it
// touches, which is when a search that turns the faces from the sixth to the
// first can look at it, every face it touches having been turned by then.
struct Seams {
    std::array<std::vector<std::array<std::uint16_t, 2>>, 6> edges;
    std::array<std::vector<std::array<std::uint16_t, 3>>, 6> corners;
};

[[nodiscard]] Seams build_seams(const Geometry& geometry) {
    Seams seams;
    const auto lowest = [&geometry](std::initializer_list<std::uint16_t> stickers) {
        std::size_t low = 6;
        for (const std::uint16_t sticker : stickers) {
            low = std::min(low, static_cast<std::size_t>(geometry.face_of(sticker)));
        }
        return low;
    };
    for (const auto& corner : geometry.corners()) {
        seams.corners.at(lowest({corner.at(0), corner.at(1), corner.at(2)})).push_back(corner);
    }
    for (const auto& orbit : geometry.wing_orbits()) {
        for (const WingSlot& slot : orbit) {
            seams.edges.at(lowest({slot.primary, slot.secondary}))
                .push_back({slot.primary, slot.secondary});
        }
    }
    if (geometry.size() % 2 == 1) {
        for (std::size_t location = 0; location < Geometry::kEdgeLocations; ++location) {
            const auto& edge = geometry.mid_edge(location);
            seams.edges.at(lowest({edge.at(0), edge.at(1)})).push_back(edge);
        }
    }
    return seams;
}

[[nodiscard]] const Seams& seams_of(int size) {
    static std::array<std::once_flag, kMaxSize + 1> built;
    static std::array<std::unique_ptr<Seams>, kMaxSize + 1> seams;
    const auto index = static_cast<std::size_t>(size);
    std::call_once(built.at(index), [&] {
        seams.at(index) = std::make_unique<Seams>(build_seams(Geometry::of(size)));
    });
    return *seams.at(index);
}

// One quarter-turn of a row-major N x N grid, as the 3x3 assembly turns one: the
// sticker that ends up at (row, col) came from (N - 1 - col, row). The search
// tries all four powers of it on every side, so which way round it is cannot
// change what can be reached.
[[nodiscard]] std::vector<Color> rotated_clockwise(const std::vector<Color>& side, int size) {
    std::vector<Color> out(side.size());
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            out.at((static_cast<std::size_t>(row) * static_cast<std::size_t>(size)) +
                   static_cast<std::size_t>(col)) =
                side.at(
                    (static_cast<std::size_t>(size - 1 - col) * static_cast<std::size_t>(size)) +
                    static_cast<std::size_t>(row));
        }
    }
    return out;
}

// The sides, each set at the cube position its centre sticker pins it to, and
// on D, R and B mirrored left to right: NCube's layout is the camera's view on
// U, F and L and its mirror on the other three, which no quarter-turn bridges.
// Which mirror is immaterial (the other differs by a half-turn, which the
// search absorbs).
struct PinnedSides {
    std::array<std::vector<Color>, 6> base;  // by Face
    std::array<std::size_t, 6> input_index_of_face{};
};

[[nodiscard]] std::optional<PinnedSides> pin_sides(int size, const ScannedSides& sides) {
    if (size < kMinSize || size > kMaxSize || size % 2 == 0) {
        return std::nullopt;
    }
    const auto n = static_cast<std::size_t>(size);
    const std::size_t middle = ((n - 1) / 2 * n) + ((n - 1) / 2);
    PinnedSides pinned;
    std::array<bool, 6> taken{};
    for (std::size_t input = 0; input < sides.size(); ++input) {
        const std::vector<Color>& side = sides.at(input);
        if (side.size() != n * n) {
            return std::nullopt;
        }
        const Face face = face_for_centre(side.at(middle));
        const auto slot = static_cast<std::size_t>(face);
        if (taken.at(slot)) {
            return std::nullopt;
        }
        taken.at(slot) = true;
        pinned.input_index_of_face.at(slot) = input;
        pinned.base.at(slot) = side;
        if (face == Face::D || face == Face::R || face == Face::B) {
            for (std::size_t row = 0; row < n; ++row) {
                std::ranges::reverse(
                    pinned.base.at(slot).begin() + static_cast<std::ptrdiff_t>(row * n),
                    pinned.base.at(slot).begin() + static_cast<std::ptrdiff_t>((row + 1) * n));
            }
        }
    }
    return pinned;
}

// Every colour is on N^2 stickers (turning a side only moves them about it), or
// no combination is a cube.
[[nodiscard]] bool colour_counts_hold(int size, const std::array<std::vector<Color>, 6>& base) {
    std::array<std::size_t, 6> counts{};
    for (const auto& side : base) {
        for (const Color colour : side) {
            ++counts.at(static_cast<std::size_t>(colour));
        }
    }
    const auto each = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    return std::ranges::all_of(counts, [each](std::size_t count) { return count == each; });
}

// The 4^6 turns of the sides, in the order of a base-4 number with face 0 the
// lowest digit, and each cube they make that validate() passes handed to
// `visit`, which says whether to go on. As the 3x3 search does it, the faces
// are turned from the sixth to the first, a combination is given up as soon as
// the faces turned so far put two stickers of one axis pair on a piece they all
// share, and a side that looks the same turned is tried once. What that skips
// is only what validate() would have refused or an earlier combination had
// already made.
// The four turns of each side, and of those the ones that differ: a side that looks the same
// turned a quarter, a half or three quarters (a side of one colour looks the same four ways)
// is tried once, the cube a repeat makes being the cube the first made.
struct Turns {
    std::array<std::array<std::vector<Color>, 4>, 6> sides;
    std::array<std::array<std::size_t, 4>, 6> distinct_quarters{};
    std::array<std::size_t, 6> distinct{};
};

[[nodiscard]] Turns turns_of(int size, const std::array<std::vector<Color>, 6>& base) {
    Turns turns;
    for (std::size_t face = 0; face < 6; ++face) {
        auto& rotations = turns.sides.at(face);
        rotations.at(0) = base.at(face);
        for (std::size_t quarter = 1; quarter < 4; ++quarter) {
            rotations.at(quarter) = rotated_clockwise(rotations.at(quarter - 1), size);
        }
        for (std::size_t quarter = 0; quarter < 4; ++quarter) {
            const auto known =
                std::span(turns.distinct_quarters.at(face)).first(turns.distinct.at(face));
            const bool seen = std::ranges::any_of(known, [&](std::size_t before) {
                return rotations.at(before) == rotations.at(quarter);
            });
            if (!seen) {
                turns.distinct_quarters.at(face).at(turns.distinct.at(face)++) = quarter;
            }
        }
    }
    return turns;
}

// Whether the pieces whose last face (in the order the search turns them) is `face` are
// possible: no edge or corner of them shows two stickers of one axis pair.
[[nodiscard]] bool seams_hold(const Seams& seams, std::size_t face,
                              const std::vector<Color>& candidate) {
    const auto pair = [&candidate](std::uint16_t sticker) {
        return axis_pair_of(candidate.at(sticker));
    };
    const bool edges_hold = std::ranges::all_of(seams.edges.at(face), [&](const auto& edge) {
        return pair(edge.at(0)) != pair(edge.at(1));
    });
    return edges_hold && std::ranges::all_of(seams.corners.at(face), [&](const auto& corner) {
               const int a = pair(corner.at(0));
               const int b = pair(corner.at(1));
               const int c = pair(corner.at(2));
               return a != b && a != c && b != c;
           });
}

template <typename Visit>
void walk_cubes(int size, const PinnedSides& pinned, const Visit& visit) {
    if (!colour_counts_hold(size, pinned.base)) {
        return;
    }
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    const Turns turns = turns_of(size, pinned.base);
    const Seams& seams = seams_of(size);
    std::vector<Color> candidate(6 * area, Color::White);

    bool going = true;
    std::uint64_t nodes = 0;
    const solver::detail::SearchWorkScope work([&nodes] { return nodes; });
    const auto turn = [&](const auto& self, std::size_t face) -> void {
        for (std::size_t at = 0; at < turns.distinct.at(face) && going; ++at) {
            const std::vector<Color>& side =
                turns.sides.at(face).at(turns.distinct_quarters.at(face).at(at));
            std::ranges::copy(side, candidate.begin() + static_cast<std::ptrdiff_t>(face * area));
            ++nodes;
            if (!seams_hold(seams, face, candidate)) {
                continue;
            }
            if (face > 0) {
                self(self, face - 1);
            } else if (validate(size, candidate).ok()) {
                going = visit(candidate);
            }
        }
    };
    turn(turn, 5);
}

// What an even size's search looks at: every piece's stickers as the faces they are on,
// so that a piece is looked at when the last of its faces is placed.
struct PieceSeam {
    std::vector<std::uint16_t> stickers;
    std::uint8_t faces = 0;  // a bit for each face (Face's number) a sticker is on
};

[[nodiscard]] std::vector<PieceSeam> piece_seams(const Geometry& geometry) {
    std::vector<PieceSeam> seams;
    const auto add = [&](std::initializer_list<std::uint16_t> stickers) {
        PieceSeam seam;
        for (const std::uint16_t sticker : stickers) {
            seam.stickers.push_back(sticker);
            seam.faces = static_cast<std::uint8_t>(
                seam.faces | (1U << static_cast<unsigned>(geometry.face_of(sticker))));
        }
        seams.push_back(std::move(seam));
    };
    for (const auto& corner : geometry.corners()) {
        add({corner.at(0), corner.at(1), corner.at(2)});
    }
    for (const auto& orbit : geometry.wing_orbits()) {
        for (const WingSlot& slot : orbit) {
            add({slot.primary, slot.secondary});
        }
    }
    return seams;
}

// Whether the stickers of a piece are all of different colours.
[[nodiscard]] bool shows_distinct_colours(const PieceSeam& seam, const std::vector<Color>& laid) {
    for (std::size_t i = 0; i < seam.stickers.size(); ++i) {
        for (std::size_t j = i + 1; j < seam.stickers.size(); ++j) {
            if (laid.at(seam.stickers.at(i)) == laid.at(seam.stickers.at(j))) {
                return false;
            }
        }
    }
    return true;
}

// The side as laid on `face`, mirrored left to right on D, R and B as the odd sizes' are, in
// each of its turns that is not a repeat of another.
[[nodiscard]] std::vector<std::vector<Color>> variants_of(const std::vector<Color>& side, Face at,
                                                          int size) {
    std::vector<Color> base = side;
    if (at == Face::D || at == Face::R || at == Face::B) {
        const auto n = static_cast<std::ptrdiff_t>(size);
        for (std::ptrdiff_t row = 0; row < n; ++row) {
            std::ranges::reverse(base.begin() + (row * n), base.begin() + ((row + 1) * n));
        }
    }
    std::vector<std::vector<Color>> list;
    for (int quarter = 0; quarter < 4; ++quarter) {
        if (std::ranges::find(list, base) == list.end()) {
            list.push_back(base);
        }
        base = rotated_clockwise(base, size);
    }
    return list;
}

// The search of an even size's sides (see walk_even): sides laid on the cube one at a time.
class EvenSearch {
public:
    EvenSearch(int size, const ScannedSides& sides)
        : size_(size),
          area_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size)),
          seams_(piece_seams(Geometry::of(size))),
          candidate_(6 * area_, Color::White) {
        for (std::size_t side = 0; side < 6; ++side) {
            for (std::size_t face = 0; face < 6; ++face) {
                variants_.at(side).at(face) =
                    variants_of(sides.at(side), static_cast<Face>(face), size);
            }
        }
    }

    // Every cube the sides make, each handed to `visit`, which says whether to go on.
    template <typename Visit>
    void run(const Visit& visit) {
        std::uint64_t nodes = 0;
        const solver::detail::SearchWorkScope work([&nodes] { return nodes; });
        bool going = true;
        lay(0, visit, nodes, going);
    }

private:
    // A piece whose last face has just been laid shows no colour twice.
    [[nodiscard]] bool pieces_hold(std::size_t face) const {
        const auto bit = static_cast<std::uint8_t>(1U << face);
        return std::ranges::all_of(seams_, [&](const PieceSeam& seam) {
            if ((seam.faces & bit) == 0 || (seam.faces & placed_) != seam.faces) {
                return true;
            }
            return shows_distinct_colours(seam, candidate_);
        });
    }

    template <typename Visit>
    void lay(std::size_t side, const Visit& visit, std::uint64_t& nodes, bool& going) {
        // The first side is on the top and turned no way; the others on any face still free.
        const std::size_t last_face = side == 0 ? 1 : 6;
        for (std::size_t face = 0; face < last_face && going; ++face) {
            if ((placed_ & (1U << face)) == 0) {
                lay_on(side, face, visit, nodes, going);
            }
        }
    }

    template <typename Visit>
    void lay_on(std::size_t side, std::size_t face, const Visit& visit, std::uint64_t& nodes,
                bool& going) {
        const auto& options = variants_.at(side).at(face);
        const std::size_t tried = side == 0 ? 1 : options.size();
        for (std::size_t at = 0; at < tried && going; ++at) {
            std::ranges::copy(options.at(at),
                              candidate_.begin() + static_cast<std::ptrdiff_t>(face * area_));
            placed_ = static_cast<std::uint8_t>(placed_ | (1U << face));
            ++nodes;
            if (pieces_hold(face)) {
                if (side < 5) {
                    lay(side + 1, visit, nodes, going);
                } else if (validate(size_, candidate_).ok()) {
                    going = visit(candidate_);
                }
            }
            placed_ = static_cast<std::uint8_t>(placed_ & ~(1U << face));
        }
    }

    int size_;
    std::size_t area_;
    std::vector<PieceSeam> seams_;
    std::vector<Color> candidate_;
    std::array<std::array<std::vector<std::vector<Color>>, 6>, 6> variants_;
    std::uint8_t placed_ = 0;
};

// The sides of an even size, which have no middle sticker to say which side of the cube
// each is, put together by trying every way of laying them on the cube.
//
// The first side is laid on the top, unturned: a cube held any other way up is the same
// cube turned, so it is the one frame that is looked in. Each of the other five goes on any
// face still free, in any of its four turns (and mirrored left to right on D, R and B, as
// the odd sizes' are), which is 5! x 4^5 = 122,880 at the most. A piece (a corner, a wing)
// is looked at as soon as its last face is down, and one that shows a colour twice is no
// piece, which stops a branch where it is. What is left at the end is asked of validate(),
// which for an even size reads the colour scheme off the corners.
template <typename Visit>
void walk_even(int size, const ScannedSides& sides, const Visit& visit) {
    const auto area = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    std::array<std::size_t, 6> counts{};
    for (const auto& side : sides) {
        if (side.size() != area) {
            return;
        }
        for (const Color colour : side) {
            ++counts.at(static_cast<std::size_t>(colour));
        }
    }
    if (!std::ranges::all_of(counts, [area](std::size_t count) { return count == area; })) {
        return;
    }
    EvenSearch(size, sides).run(visit);
}

}  // namespace

std::vector<NCube> assemble_all_scanned_sides(int size, const ScannedSides& sides,
                                              std::size_t limit) {
    CUBE_ZONE_SCOPED_N("nxn.assemble_scan");
    std::vector<NCube> survivors;
    if (limit == 0) {
        return survivors;
    }
    if (size >= kMinSize && size <= kMaxSize && size % 2 == 0) {
        walk_even(size, sides, [&](const std::vector<Color>& facelets) {
            const Result<NCube> cube = NCube::from_facelets(size, facelets);
            if (cube && std::ranges::find(survivors, *cube) == survivors.end()) {
                survivors.push_back(*cube);
            }
            return survivors.size() < limit;
        });
        return survivors;
    }
    const std::optional<PinnedSides> pinned = pin_sides(size, sides);
    if (!pinned) {
        return survivors;
    }
    walk_cubes(size, *pinned, [&](const std::vector<Color>& facelets) {
        const Result<NCube> cube = NCube::from_facelets(size, facelets);
        if (cube && std::ranges::find(survivors, *cube) == survivors.end()) {
            survivors.push_back(*cube);
        }
        return survivors.size() < limit;
    });
    return survivors;
}

Result<NCube> assemble_scanned_sides(int size, const ScannedSides& sides) {
    // Two are enough to know it is not one.
    std::vector<NCube> cubes = assemble_all_scanned_sides(size, sides, 2);
    if (cubes.size() != 1) {
        return std::unexpected(CubeError::invalid_cube_state);
    }
    return std::move(cubes.front());
}

}  // namespace cube::nxn
