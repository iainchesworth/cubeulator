#include "controllers/cube_edit.hpp"

#include <cube/nxn/cube.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <utility>

namespace cube::app {

namespace {

using cube::nxn::Face;

constexpr std::array<char, kColourCount> kLetters = {'W', 'Y', 'G', 'B', 'O', 'R'};

// Where the sticker at (row, col) as seen from outside is in the library's
// numbering of its side, and the other way round: the same picture on U, F and L,
// mirrored left to right on R and B and top to bottom on D, which undoes itself
// (cube::nxn::outside_view).
[[nodiscard]] std::pair<int, int> mirrored(Face face, int size, int row, int col) {
    switch (face) {
        case Face::R:
        case Face::B:
            return {row, size - 1 - col};
        case Face::D:
            return {size - 1 - row, col};
        case Face::U:
        case Face::F:
        case Face::L:
            break;
    }
    return {row, col};
}

[[nodiscard]] bool is_colour(std::int8_t colour) noexcept {
    return colour >= 0 && colour < kColourCount;
}

[[nodiscard]] bool paints_or_clears(std::int8_t colour) noexcept {
    return colour == kUnpainted || is_colour(colour);
}

}  // namespace

std::size_t seen_facelet(Face face, int size, int index) {
    const auto n = static_cast<std::size_t>(size);
    const auto [row, col] = mirrored(face, size, index / size, index % size);
    return (static_cast<std::size_t>(face) * n * n) + (static_cast<std::size_t>(row) * n) +
           static_cast<std::size_t>(col);
}

char colour_letter(std::int8_t colour) noexcept {
    return is_colour(colour) ? kLetters.at(static_cast<std::size_t>(colour)) : '?';
}

SideText parse_side(std::string_view text, int size) {
    SideText out;
    const auto wanted = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    int given = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        const auto c = static_cast<unsigned char>(text[at]);
        if (std::isspace(c) != 0 || c == ',' || c == '/' || c == '|' || c == ';') {
            continue;
        }
        std::int8_t colour = kUnpainted;
        if (c != '?' && c != '.') {
            const char upper = static_cast<char>(std::toupper(c));
            const auto found = std::ranges::find(kLetters, upper);
            if (found == kLetters.end()) {
                out.colours.clear();
                out.error = "unknownLetter";
                out.at = static_cast<int>(at);
                return out;
            }
            colour = static_cast<std::int8_t>(std::distance(kLetters.begin(), found));
        }
        ++given;
        if (out.colours.size() < wanted) {
            out.colours.push_back(colour);
        }
    }
    if (static_cast<std::size_t>(given) < wanted) {
        out.colours.clear();
        out.error = "tooFew";
        out.at = given;
    } else if (static_cast<std::size_t>(given) > wanted) {
        out.colours.clear();
        out.error = "tooMany";
        out.at = given;
    }
    return out;
}

CubeEdit::CubeEdit(int size) {
    begin(size);
}

void CubeEdit::begin(int size) {
    size_ = std::clamp(size, cube::nxn::kMinSize, cube::nxn::kMaxSize);
    const auto n = static_cast<std::size_t>(size_);
    from_reading_ = false;
    upright_ = true;
    stickers_.assign(6 * n * n, kUnpainted);
    doubtful_.assign(6 * n * n, 0);
    origin_ = snapshot();
    undo_.clear();
    redo_.clear();
    stroke_open_ = false;
    stroke_started_ = false;
}

void CubeEdit::load(Reading reading) {
    begin(reading.size);
    const auto n = static_cast<std::size_t>(size_);
    if (reading.stickers.size() != 6 * n * n) {
        return;
    }
    for (std::int8_t& colour : reading.stickers) {
        if (!is_colour(colour)) {
            colour = kUnpainted;
        }
    }
    stickers_ = std::move(reading.stickers);
    if (reading.doubtful.size() == stickers_.size()) {
        doubtful_ = std::move(reading.doubtful);
    }
    // A sticker nothing was read for is not a doubt, it is a gap.
    for (std::size_t i = 0; i < stickers_.size(); ++i) {
        if (stickers_.at(i) == kUnpainted) {
            doubtful_.at(i) = 0;
        }
    }
    from_reading_ = true;
    upright_ = reading.upright;
    origin_ = snapshot();
}

std::int8_t CubeEdit::colour(Face face, int index) const {
    if (index < 0 || index >= size_ * size_) {
        return kUnpainted;
    }
    return stickers_.at(facelet(face, index));
}

bool CubeEdit::doubtful(Face face, int index) const {
    if (index < 0 || index >= size_ * size_) {
        return false;
    }
    return doubtful_.at(facelet(face, index)) != 0;
}

std::int8_t CubeEdit::centre(Face face) const {
    if (size_ % 2 == 0) {
        return kUnpainted;
    }
    const int middle = size_ / 2;
    return colour(face, (middle * size_) + middle);
}

int CubeEdit::painted_on(Face face) const {
    int count = 0;
    for (int i = 0; i < size_ * size_; ++i) {
        count += colour(face, i) == kUnpainted ? 0 : 1;
    }
    return count;
}

int CubeEdit::doubtful_on(Face face) const {
    int count = 0;
    for (int i = 0; i < size_ * size_; ++i) {
        count += doubtful(face, i) ? 1 : 0;
    }
    return count;
}

EditStatus CubeEdit::status() const {
    const auto n = static_cast<std::size_t>(size_);
    const int per_colour = size_ * size_;
    std::array<int, kColourCount> counts{};
    int to_go = 0;
    for (const std::int8_t colour : stickers_) {
        if (colour == kUnpainted) {
            ++to_go;
        } else {
            ++counts.at(static_cast<std::size_t>(colour));
        }
    }

    EditStatus out;
    out.to_go = to_go;
    if (static_cast<std::size_t>(to_go) == 6 * n * n) {
        out.state = EditState::Empty;
        return out;
    }
    if (to_go > 0) {
        out.state = EditState::Partial;
        return out;
    }
    for (int colour = 0; colour < kColourCount; ++colour) {
        const int count = counts.at(static_cast<std::size_t>(colour));
        if (count == per_colour) {
            continue;
        }
        (count > per_colour ? out.over : out.under)
            .push_back({.colour = static_cast<std::int8_t>(colour), .count = count});
    }
    if (!out.over.empty() || !out.under.empty()) {
        out.state = EditState::Miscount;
        return out;
    }

    std::vector<cube::solver::Color> colours;
    colours.reserve(stickers_.size());
    for (const std::int8_t colour : stickers_) {
        colours.push_back(static_cast<cube::solver::Color>(colour));
    }
    const cube::nxn::Verdict verdict = cube::nxn::validate(size_, colours);
    if (verdict.ok()) {
        out.state = EditState::Valid;
        return out;
    }
    out.state = EditState::Impossible;
    out.fault = verdict.fault;
    using cube::nxn::Fault;
    const bool about_a_piece =
        verdict.fault == Fault::TwistedCorner || verdict.fault == Fault::FlippedEdge ||
        verdict.fault == Fault::UnknownPiece || verdict.fault == Fault::RepeatedPiece;
    if (about_a_piece) {
        const auto position = cube::nxn::Geometry::of(size_).position(verdict.facelet);
        const auto [row, col] = mirrored(position.face, size_, position.row, position.col);
        out.suspect = EditStatus::Place{.face = position.face, .index = (row * size_) + col};
    }
    return out;
}

void CubeEdit::begin_stroke() noexcept {
    if (!stroke_open_) {
        stroke_open_ = true;
        stroke_started_ = false;
    }
}

void CubeEdit::end_stroke() noexcept {
    stroke_open_ = false;
    stroke_started_ = false;
}

void CubeEdit::checkpoint() {
    if (stroke_open_ && stroke_started_) {
        return;
    }
    stroke_started_ = stroke_open_;
    undo_.push_back(snapshot());
    if (undo_.size() > kMostSteps) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

bool CubeEdit::paint(Face face, int index, std::int8_t colour) {
    if (index < 0 || index >= size_ * size_ || !paints_or_clears(colour)) {
        return false;
    }
    const std::size_t at = facelet(face, index);
    if (stickers_.at(at) == colour && doubtful_.at(at) == 0) {
        return false;
    }
    checkpoint();
    stickers_.at(at) = colour;
    doubtful_.at(at) = 0;
    return true;
}

bool CubeEdit::fill(Face face, std::int8_t colour) {
    if (!paints_or_clears(colour)) {
        return false;
    }
    std::vector<std::int8_t> all(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_),
                                 colour);
    return set_side(face, all);
}

bool CubeEdit::set_side(Face face, const std::vector<std::int8_t>& colours) {
    if (colours.size() != static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) ||
        !std::ranges::all_of(colours, paints_or_clears)) {
        return false;
    }
    bool differs = false;
    for (int i = 0; i < size_ * size_; ++i) {
        const std::size_t at = facelet(face, i);
        differs = differs || stickers_.at(at) != colours.at(static_cast<std::size_t>(i)) ||
                  doubtful_.at(at) != 0;
    }
    if (!differs) {
        return false;
    }
    checkpoint();
    for (int i = 0; i < size_ * size_; ++i) {
        const std::size_t at = facelet(face, i);
        stickers_.at(at) = colours.at(static_cast<std::size_t>(i));
        doubtful_.at(at) = 0;
    }
    return true;
}

bool CubeEdit::rotate_side(Face face, int quarter_turns) {
    const int turns = ((quarter_turns % 4) + 4) % 4;
    if (turns == 0) {
        return false;
    }
    std::vector<std::int8_t> now(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_));
    for (int i = 0; i < size_ * size_; ++i) {
        now.at(static_cast<std::size_t>(i)) = colour(face, i);
    }
    // Clockwise: the sticker at (row, col) goes to (col, size - 1 - row).
    std::vector<std::int8_t> turned = now;
    for (int t = 0; t < turns; ++t) {
        std::vector<std::int8_t> next(turned.size());
        for (int row = 0; row < size_; ++row) {
            for (int col = 0; col < size_; ++col) {
                next.at(static_cast<std::size_t>((col * size_) + (size_ - 1 - row))) =
                    turned.at(static_cast<std::size_t>((row * size_) + col));
            }
        }
        turned = std::move(next);
    }
    if (turned == now) {
        return false;
    }
    // The doubt travels with its sticker.
    std::vector<std::uint8_t> doubts(now.size());
    for (int i = 0; i < size_ * size_; ++i) {
        doubts.at(static_cast<std::size_t>(i)) = doubtful(face, i) ? 1 : 0;
    }
    std::vector<std::uint8_t> moved = doubts;
    for (int t = 0; t < turns; ++t) {
        std::vector<std::uint8_t> next(moved.size());
        for (int row = 0; row < size_; ++row) {
            for (int col = 0; col < size_; ++col) {
                next.at(static_cast<std::size_t>((col * size_) + (size_ - 1 - row))) =
                    moved.at(static_cast<std::size_t>((row * size_) + col));
            }
        }
        moved = std::move(next);
    }
    checkpoint();
    for (int i = 0; i < size_ * size_; ++i) {
        const std::size_t at = facelet(face, i);
        stickers_.at(at) = turned.at(static_cast<std::size_t>(i));
        doubtful_.at(at) = moved.at(static_cast<std::size_t>(i));
    }
    return true;
}

bool CubeEdit::clear_all() {
    if (std::ranges::all_of(stickers_, [](std::int8_t c) { return c == kUnpainted; }) &&
        std::ranges::all_of(doubtful_, [](std::uint8_t d) { return d == 0; })) {
        return false;
    }
    checkpoint();
    std::ranges::fill(stickers_, kUnpainted);
    std::ranges::fill(doubtful_, std::uint8_t{0});
    return true;
}

bool CubeEdit::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(snapshot());
    stickers_ = std::move(undo_.back().stickers);
    doubtful_ = std::move(undo_.back().doubtful);
    undo_.pop_back();
    stroke_open_ = false;
    stroke_started_ = false;
    return true;
}

bool CubeEdit::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(snapshot());
    stickers_ = std::move(redo_.back().stickers);
    doubtful_ = std::move(redo_.back().doubtful);
    redo_.pop_back();
    stroke_open_ = false;
    stroke_started_ = false;
    return true;
}

bool CubeEdit::revert() {
    if (stickers_ == origin_.stickers && doubtful_ == origin_.doubtful) {
        return false;
    }
    checkpoint();
    stickers_ = origin_.stickers;
    doubtful_ = origin_.doubtful;
    return true;
}

bool CubeEdit::changed() const {
    return stickers_ != origin_.stickers;
}

}  // namespace cube::app
