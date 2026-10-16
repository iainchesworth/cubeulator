#include <cube/nxn/notation.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

namespace cube::nxn {

namespace {

[[nodiscard]] constexpr char face_letter(Face face) noexcept {
    switch (face) {
        case Face::U:
            return 'U';
        case Face::D:
            return 'D';
        case Face::L:
            return 'L';
        case Face::R:
            return 'R';
        case Face::F:
            return 'F';
        case Face::B:
            return 'B';
    }
    return '?';
}

[[nodiscard]] constexpr char axis_letter(Axis axis) noexcept {
    switch (axis) {
        case Axis::X:
            return 'x';
        case Axis::Y:
            return 'y';
        case Axis::Z:
            return 'z';
    }
    return '?';
}

[[nodiscard]] constexpr std::optional<Face> face_named(char letter) noexcept {
    switch (letter) {
        case 'U':
            return Face::U;
        case 'D':
            return Face::D;
        case 'L':
            return Face::L;
        case 'R':
            return Face::R;
        case 'F':
            return Face::F;
        case 'B':
            return Face::B;
        default:
            return std::nullopt;
    }
}

// The turn of the whole cube a letter names: the face it turns as.
[[nodiscard]] constexpr std::optional<Face> rotation_named(char letter) noexcept {
    switch (letter) {
        case 'x':
            return Face::R;
        case 'y':
            return Face::U;
        case 'z':
            return Face::F;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// The capital of a lower-case letter, or '?' for any other character.
[[nodiscard]] constexpr char upper_case(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : '?';
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// Reads one token from the front.
class Cursor {
public:
    explicit Cursor(std::string_view text) : text_(text) {}

    [[nodiscard]] bool done() const noexcept { return at_ == text_.size(); }

    // The next character, or '\0' at the end.
    [[nodiscard]] char peek() const noexcept { return done() ? '\0' : text_.at(at_); }

    // Takes the next character if it is `expected`.
    bool accept(char expected) noexcept {
        if (peek() != expected || done()) {
            return false;
        }
        ++at_;
        return true;
    }

    char take() noexcept {
        const char c = peek();
        if (!done()) {
            ++at_;
        }
        return c;
    }

    // A run of digits, if there is one. Past four digits it saturates, which no
    // cube has the layers for.
    std::optional<int> number() noexcept {
        if (!is_digit(peek())) {
            return std::nullopt;
        }
        int value = 0;
        while (is_digit(peek())) {
            value = std::min((value * 10) + (take() - '0'), 9999);
        }
        return value;
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;
};

using Kind = ParseFault::Kind;
using TokenResult = std::expected<WideMove, Kind>;

// The layer numbers written before a letter: none, one (2R, 3Rw), or a range
// (2-3R).
struct Written {
    std::optional<int> from;
    std::optional<int> to;
};

[[nodiscard]] std::expected<Written, Kind> read_written(Cursor& cursor) {
    Written written;
    written.from = cursor.number();
    if (written.from && cursor.accept('-')) {
        written.to = cursor.number();
        if (!written.to) {
            return std::unexpected(Kind::UnknownMove);
        }
    }
    return written;
}

// How far a letter's move reaches into the cube.
enum class Reach : std::uint8_t {
    // The layers the number names, or the outer layer if it names none.
    Named,
    // The outer layers, as many as the number says, or two if it says none.
    Wide,
    // Every layer: a turn of the whole cube.
    Whole,
};

struct Letter {
    Face face = Face::U;
    Reach reach = Reach::Named;
};

// What the letter names: a face (turning its outer layer, or with a w several),
// a face spelled in lower case (the old way of writing two layers), or a
// rotation of the whole cube. Neither of the last two takes a layer number.
[[nodiscard]] std::optional<Letter> read_letter(Cursor& cursor, bool numbered) {
    const char letter = cursor.take();
    if (const std::optional<Face> face = face_named(letter)) {
        return Letter{.face = *face, .reach = cursor.accept('w') ? Reach::Wide : Reach::Named};
    }
    if (numbered) {
        return std::nullopt;
    }
    if (const std::optional<Face> face = face_named(upper_case(letter))) {
        return Letter{.face = *face, .reach = Reach::Wide};
    }
    if (const std::optional<Face> face = rotation_named(letter)) {
        return Letter{.face = *face, .reach = Reach::Whole};
    }
    return std::nullopt;
}

// Nothing, a prime, a 2, or a 2 and a prime (which is a half turn all the same).
[[nodiscard]] std::optional<int> read_turns(Cursor& cursor) {
    int turns = 1;
    if (cursor.accept('2')) {
        turns = 2;
    }
    if (cursor.accept('\'')) {
        turns = turns == 2 ? 2 : 3;
    }
    return cursor.done() ? std::optional<int>{turns} : std::nullopt;
}

struct Depths {
    int first = 0;
    int last = 0;
};

[[nodiscard]] std::expected<Depths, Kind> depths_of(Reach reach, const Written& written, int size) {
    switch (reach) {
        case Reach::Whole:
            return Depths{.first = 0, .last = size - 1};
        case Reach::Wide: {
            if (written.to) {
                return std::unexpected(Kind::UnknownMove);  // there is no 2-3Rw
            }
            const int layers = written.from.value_or(2);
            if (layers < 1 || layers > size) {
                return std::unexpected(Kind::LayerOutOfRange);
            }
            return Depths{.first = 0, .last = layers - 1};
        }
        case Reach::Named:
            break;
    }
    if (!written.from) {
        return Depths{.first = 0, .last = 0};
    }
    const int from = *written.from;
    const int to = written.to.value_or(from);
    if (from < 1 || to > size || from > to) {
        return std::unexpected(Kind::LayerOutOfRange);
    }
    return Depths{.first = from - 1, .last = to - 1};
}

[[nodiscard]] TokenResult parse_token(std::string_view token, int size) {
    Cursor cursor{token};
    const std::expected<Written, Kind> written = read_written(cursor);
    if (!written) {
        return std::unexpected(written.error());
    }
    const std::optional<Letter> letter = read_letter(cursor, written->from.has_value());
    if (!letter) {
        return std::unexpected(Kind::UnknownMove);
    }
    const std::optional<int> turns = read_turns(cursor);
    if (!turns) {
        return std::unexpected(Kind::UnknownMove);
    }
    const std::expected<Depths, Kind> depths = depths_of(letter->reach, *written, size);
    if (!depths) {
        return std::unexpected(depths.error());
    }
    return WideMove{
        .face = letter->face,
        .first = depths->first,
        .last = depths->last,
        .turns = *turns,
    };
}

[[nodiscard]] std::string_view turn_suffix(int turns) noexcept {
    switch (((turns % 4) + 4) % 4) {
        case 1:
            return "";
        case 2:
            return "2";
        case 3:
            return "'";
        default:
            return "";
    }
}

// A block of layers lo..hi of `axis` (counted from its negative end) turned
// `turns` clockwise as seen from the positive end, spelled from the nearer end.
[[nodiscard]] WideMove block_on(Axis axis, int lo, int hi, int turns, int size) noexcept {
    const int from_negative = lo;
    const int from_positive = size - 1 - hi;
    if (from_negative < from_positive) {
        return WideMove{.face = face_on(axis, false), .first = lo, .last = hi, .turns = 4 - turns};
    }
    return WideMove{
        .face = face_on(axis, true),
        .first = from_positive,
        .last = size - 1 - lo,
        .turns = turns,
    };
}

}  // namespace

std::vector<LayerMove> layers_of(const WideMove& move) {
    std::vector<LayerMove> layers;
    layers.reserve(static_cast<std::size_t>(move.layer_count()));
    for (int depth = move.first; depth <= move.last; ++depth) {
        layers.push_back(LayerMove{.face = move.face, .depth = depth, .turns = move.turns});
    }
    return layers;
}

std::vector<LayerMove> layers_of(std::span<const WideMove> moves) {
    std::vector<LayerMove> layers;
    for (const WideMove& move : moves) {
        for (int depth = move.first; depth <= move.last; ++depth) {
            layers.push_back(LayerMove{.face = move.face, .depth = depth, .turns = move.turns});
        }
    }
    return layers;
}

WideMove canonical(const WideMove& move, int size) {
    const bool positive = is_positive(move.face);
    const int lo = positive ? size - 1 - move.last : move.first;
    const int hi = positive ? size - 1 - move.first : move.last;
    const int turns = positive ? move.turns : 4 - move.turns;
    return block_on(axis_of(move.face), lo, hi, turns, size);
}

std::expected<std::vector<WideMove>, ParseFault> parse(std::string_view text, int size) {
    if (size < kMinSize || size > kMaxSize) {
        return std::unexpected(
            ParseFault{.kind = ParseFault::Kind::UnsupportedSize, .offset = 0, .length = 0});
    }
    std::vector<WideMove> moves;
    std::size_t at = 0;
    while (true) {
        while (at < text.size() && is_space(text.at(at))) {
            ++at;
        }
        if (at == text.size()) {
            return moves;
        }
        const std::size_t start = at;
        while (at < text.size() && !is_space(text.at(at))) {
            ++at;
        }
        const std::string_view token = text.substr(start, at - start);
        const TokenResult move = parse_token(token, size);
        if (!move) {
            return std::unexpected(
                ParseFault{.kind = move.error(), .offset = start, .length = token.size()});
        }
        moves.push_back(*move);
    }
}

std::string to_string(const WideMove& move, int size) {
    assert(move.first >= 0 && move.first <= move.last && move.last < size);
    std::string text;
    int turns = move.turns;
    if (move.first == 0 && move.last == size - 1) {
        // Every layer of the axis: the whole cube turns, and is named for the
        // way the positive face turns.
        text += axis_letter(axis_of(move.face));
        turns = is_positive(move.face) ? move.turns : 4 - move.turns;
    } else if (move.first == 0) {
        if (move.last >= 2) {
            text += std::to_string(move.last + 1);
        }
        text += face_letter(move.face);
        if (move.last >= 1) {
            text += 'w';
        }
    } else if (move.first == move.last) {
        text += std::to_string(move.first + 1);
        text += face_letter(move.face);
    } else {
        text += std::to_string(move.first + 1);
        text += '-';
        text += std::to_string(move.last + 1);
        text += face_letter(move.face);
    }
    text += turn_suffix(turns);
    return text;
}

std::string to_string(LayerMove move, int size) {
    return to_string(
        WideMove{.face = move.face, .first = move.depth, .last = move.depth, .turns = move.turns},
        size);
}

std::string to_string(std::span<const WideMove> moves, int size) {
    std::string text;
    for (const WideMove& move : moves) {
        if (!text.empty()) {
            text += ' ';
        }
        text += to_string(move, size);
    }
    return text;
}

std::string to_string(std::span<const LayerMove> moves, int size) {
    std::string text;
    for (const LayerMove& move : moves) {
        if (!text.empty()) {
            text += ' ';
        }
        text += to_string(move, size);
    }
    return text;
}

namespace {

// A turn of one layer in a run of turns about an axis: its angle, and where in
// the run it came.
struct RunTurn {
    int turns = 1;
    std::size_t order = 0;
};

// The turns left in a run, by layer, in the order they came.
using RunTurns = std::vector<std::vector<RunTurn>>;

// A turn looked for: of this layer, through this angle.
struct Wanted {
    int layer = 0;
    int turns = 1;
};

// Removes the first turn like `wanted` and gives its place in the run, if
// there is one.
std::optional<std::size_t> take_turn(RunTurns& run, Wanted wanted) {
    if (wanted.layer < 0 || std::cmp_greater_equal(wanted.layer, run.size())) {
        return std::nullopt;
    }
    std::vector<RunTurn>& at_layer = run.at(static_cast<std::size_t>(wanted.layer));
    const auto found = std::ranges::find_if(
        at_layer, [turns = wanted.turns](const RunTurn& t) { return t.turns == turns; });
    if (found == at_layer.end()) {
        return std::nullopt;
    }
    const std::size_t order = found->order;
    at_layer.erase(found);
    return order;
}

// A block, and the place in the run of the first of its turns.
struct Placed {
    WideMove block;
    std::size_t order = 0;
};

// Gathers one run of turns about `axis` into blocks: take the lowest layer that
// has a turn left, and add the next layer up while it has a turn of the same
// angle. The blocks come out in the order their first turn went in, so a run
// with nothing to gather is as it was.
void group_run(Axis axis, const std::vector<SliceTurn>& turns, int size,
               std::vector<WideMove>& blocks) {
    RunTurns run(static_cast<std::size_t>(size));
    for (std::size_t order = 0; order < turns.size(); ++order) {
        const SliceTurn& turn = turns.at(order);
        run.at(static_cast<std::size_t>(turn.layer))
            .push_back(RunTurn{.turns = turn.turns, .order = order});
    }
    std::vector<Placed> placed;
    for (int lo = 0; lo < size; ++lo) {
        while (!run.at(static_cast<std::size_t>(lo)).empty()) {
            const int angle = run.at(static_cast<std::size_t>(lo)).front().turns;
            std::size_t first_order = take_turn(run, {.layer = lo, .turns = angle}).value_or(0);
            int hi = lo;
            while (const std::optional<std::size_t> order =
                       take_turn(run, {.layer = hi + 1, .turns = angle})) {
                first_order = std::min(first_order, *order);
                ++hi;
            }
            placed.push_back(
                Placed{.block = block_on(axis, lo, hi, angle, size), .order = first_order});
        }
    }
    std::ranges::stable_sort(placed, {}, &Placed::order);
    for (const Placed& p : placed) {
        blocks.push_back(p.block);
    }
}

}  // namespace

std::vector<WideMove> group(std::span<const LayerMove> moves, int size) {
    std::vector<WideMove> blocks;
    auto begin = moves.begin();
    while (begin != moves.end()) {
        const Axis axis = axis_of(begin->face);
        const auto end = std::ranges::find_if(begin, moves.end(), [axis](const LayerMove& move) {
            return axis_of(move.face) != axis;
        });
        std::vector<SliceTurn> run;
        for (auto it = begin; it != end; ++it) {
            run.push_back(to_slice_turn(*it, size));
        }
        group_run(axis, run, size, blocks);
        begin = end;
    }
    return blocks;
}

}  // namespace cube::nxn
