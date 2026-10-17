#pragma once

#include <cube/nxn/geometry.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/solver/cube.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The part of putting a cube right by hand that needs no screen and no Qt: the
// colours of a cube of any size, edited a sticker, a side or a stroke at a time,
// with undo, redo and a way back to where it began, and a plain answer to "is this
// a cube a person could hold?" (docs/design/screens/review.md).
//
// It starts blank (a cube typed in) or from a reading (a cube the camera read,
// to be corrected), and in both cases the same edits apply. ColourEntry puts a
// QObject round it for QML.
//
// A side is addressed as a person sees it: "as seen from outside", the top row of
// its grid first, left to right, which is the order the screen draws it in and the
// order a person would read a side out. The library numbers the stickers of R, B
// and D another way (cube::nxn::outside_view); this class is the one place that
// turns one into the other.
//
// Every colour is a cube::solver::Color number, or kUnpainted.

namespace cube::app {

inline constexpr std::int8_t kUnpainted = -1;
inline constexpr int kColourCount = 6;

// What is the matter with the cube as it stands.
enum class EditState : std::uint8_t {
    // Nothing painted.
    Empty,
    // Some stickers not painted.
    Partial,
    // Everything painted, a colour used too often or too seldom.
    Miscount,
    // Every colour N * N times, not a cube a person could hold.
    Impossible,
    // A cube.
    Valid,
};

struct ColourCount {
    std::int8_t colour = 0;
    int count = 0;
};

struct EditStatus {
    EditState state = EditState::Empty;
    // Stickers not painted.
    int to_go = 0;
    // For Miscount: the colours with too many stickers and with too few.
    std::vector<ColourCount> over;
    std::vector<ColourCount> under;
    // For Impossible: the library's reason, and - for a fault about one piece - a
    // sticker of it as seen from outside (the one to look at first).
    cube::nxn::Fault fault = cube::nxn::Fault::None;
    struct Place {
        cube::solver::Face face = cube::solver::Face::U;
        int index = 0;
    };
    std::optional<Place> suspect;
};

// A cube as a camera (or anything else) read it: the stickers in the library's
// numbering (cube::nxn::NCube), and - where the reader knew - which of them it was
// not sure of.
struct Reading {
    int size = 3;
    // 6 * N * N colours, or kUnpainted where nothing was read.
    std::vector<std::int8_t> stickers;
    // The same length, nonzero where the reader doubted the sticker; or empty.
    std::vector<std::uint8_t> doubtful;
    // False when the reader could name the colours of each side but not tell which
    // way up each side was held, so each is shown as the camera had it and may need
    // turning (CubeEdit::rotate_side) before the sides fit together.
    bool upright = true;
};

// The library's number for the sticker at `index` (row * N + col, as seen from
// outside) of `face`: where a reading puts what it saw. The mirroring lives here, once.
[[nodiscard]] std::size_t seen_facelet(cube::solver::Face face, int size, int index);

// What describing a side in words came to.
struct SideText {
    // The colours in reading order, N * N of them, when `error` is empty. A "?" or
    // "." in the text is a sticker left unpainted.
    std::vector<std::int8_t> colours;
    // "" when the text was understood; otherwise "tooFew", "tooMany" or
    // "unknownLetter".
    std::string_view error;
    // For "unknownLetter": the character position (from 0) of the first letter
    // that is no colour. For "tooFew" and "tooMany": how many colours were given.
    int at = 0;
};

// Reads a side described as letters, in reading order: W Y G B O R for white,
// yellow, green, blue, orange and red, in either case. The letters are not
// translated, as the letters of a move are not: what is typed does not change with
// the language of the screen. Spaces, commas, slashes, bars and line breaks only
// separate (a row to a line is easiest to read back), and "?" or "." leaves a
// sticker unpainted. "WWW/WWW/WWW" and "wwwwwwwww" are the same side.
[[nodiscard]] SideText parse_side(std::string_view text, int size);

// The letter a colour is typed as, for the screen to show beside it.
[[nodiscard]] char colour_letter(std::int8_t colour) noexcept;

class CubeEdit {
public:
    explicit CubeEdit(int size = 3);

    // A blank cube of `size` (clamped to the sizes the library has), with no
    // history; "where it began" is blank.
    void begin(int size);
    // Starts from `reading`, which becomes where it began: revert() comes back to
    // it. A reading the wrong length for its size is read as blank.
    void load(Reading reading);

    [[nodiscard]] int size() const noexcept { return size_; }
    // Started from a reading, not blank.
    [[nodiscard]] bool from_reading() const noexcept { return from_reading_; }
    // The reading knew which way up each side was held (Reading::upright); true for
    // a cube typed in.
    [[nodiscard]] bool upright() const noexcept { return upright_; }

    // --- What is on show ---------------------------------------------------

    // The colour of the sticker at `index` (row * N + col, as seen from outside) of
    // `face`; kUnpainted where none; kUnpainted for an index off the side.
    [[nodiscard]] std::int8_t colour(cube::solver::Face face, int index) const;
    // Whether the reader doubted that sticker and it has not been touched since.
    [[nodiscard]] bool doubtful(cube::solver::Face face, int index) const;
    // The middle sticker of a side of an odd size, which names it, or kUnpainted.
    [[nodiscard]] std::int8_t centre(cube::solver::Face face) const;
    // How many stickers of `face` are painted, and how many the reader doubted.
    [[nodiscard]] int painted_on(cube::solver::Face face) const;
    [[nodiscard]] int doubtful_on(cube::solver::Face face) const;
    // All the stickers, numbered as cube::nxn::NCube numbers them.
    [[nodiscard]] const std::vector<std::int8_t>& stickers() const noexcept { return stickers_; }

    [[nodiscard]] EditStatus status() const;

    // --- Editing -------------------------------------------------------------

    // Each of these is one step to undo, unless a stroke is open (begin_stroke),
    // and each returns whether anything changed. Painting a sticker clears the
    // doubt about it: it is now what the person says.
    bool paint(cube::solver::Face face, int index, std::int8_t colour);
    // The whole side one colour (kUnpainted clears it).
    bool fill(cube::solver::Face face, std::int8_t colour);
    // The side as a person read it out: N * N colours, reading order (SideText).
    bool set_side(cube::solver::Face face, const std::vector<std::int8_t>& colours);
    // The side turned in place, as seen from outside, by `quarter_turns` clockwise
    // (negative for the other way). The reader cannot always tell which way up a side
    // was held; this puts it the way the person has it.
    bool rotate_side(cube::solver::Face face, int quarter_turns);
    // Every sticker unpainted.
    bool clear_all();

    // Edits between begin_stroke and end_stroke are one step (a finger dragged over
    // several stickers undoes in one go). Opening one while one is open does
    // nothing.
    void begin_stroke() noexcept;
    void end_stroke() noexcept;

    // --- History -------------------------------------------------------------

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    bool undo();
    bool redo();
    // Back to where it began, as one step that can itself be undone. False when it
    // was already there.
    bool revert();
    // What a person would lose by leaving: the cube is not as it began.
    [[nodiscard]] bool changed() const;

private:
    struct Snapshot {
        std::vector<std::int8_t> stickers;
        std::vector<std::uint8_t> doubtful;
    };

    [[nodiscard]] std::size_t facelet(cube::solver::Face face, int index) const {
        return seen_facelet(face, size_, index);
    }
    // Records the state before an edit that is about to change something.
    void checkpoint();
    [[nodiscard]] Snapshot snapshot() const { return {stickers_, doubtful_}; }

    static constexpr std::size_t kMostSteps = 256;

    int size_ = 3;
    bool from_reading_ = false;
    bool upright_ = true;
    std::vector<std::int8_t> stickers_;
    std::vector<std::uint8_t> doubtful_;
    Snapshot origin_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool stroke_open_ = false;
    bool stroke_started_ = false;
};

}  // namespace cube::app
