#pragma once

#include "controllers/cube_edit.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>

class SolveController;

namespace cube::app {

class DirectScan;

// A cube typed in by hand, a colour at a time, for a cube of any size
// (docs/design/screens/review.md, "A cube of another size") - or a cube the camera
// read, to be put right where it misread (the same screen, started from the
// reading: loadReading). The editing, the counting and the asking whether it is a
// cube are CubeEdit's; this is the QObject QML talks to.
//
// The cube is held any way the person likes; the side facing up is called the
// top and the side facing them the front, and every side is painted as it looks
// from outside, the top row of its grid next to the side named for it
// (cube::nxn::outside_view). The colours are not given: whichever six colours
// there are, they are the cube's, and the library says whether they can be those
// of a cube a person could hold (cube::nxn::validate). So nothing is fixed
// beforehand, not even the middle of a side, and an even size, which has none to
// name a side by, is entered the same way as an odd one.
//
// Every number here is a colour as cube::solver::Color numbers it, or -1 for a
// sticker not painted yet; QML says them as names (colour_name.hpp).
class ColourEntry : public QObject {
    Q_OBJECT

    // The cube's side length. begin() sets it.
    Q_PROPERTY(int size READ size NOTIFY changed)
    // Every side as it is painted: side letter ("U", "D", "L", "R", "F", "B") ->
    // N * N entries, top-left first, each a colour name ("White", "Yellow",
    // "Green", "Blue", "Orange", "Red") or "" where nothing is painted.
    Q_PROPERTY(QVariantMap sides READ sides NOTIFY changed)
    // Side letter -> N * N booleans, true where the camera was not sure of the
    // sticker and nobody has painted over it since. All false for a cube typed in.
    Q_PROPERTY(QVariantMap doubts READ doubts NOTIFY changed)
    // Side letter -> the colour name of the middle sticker of an odd size, "" where
    // there is none (an even size) or nothing is painted there. A side of a read
    // cube is named by it ("the red side"), as a person holding the cube would.
    Q_PROPERTY(QVariantMap centres READ centres NOTIFY changed)
    // What is the matter with it, for the line beside the Solve button:
    //   state:    "empty" (nothing painted), "partial" (some stickers to go),
    //             "miscount" (all painted, a colour used too often or too seldom),
    //             "impossible" (every colour N*N times, but not a cube), "valid".
    //   toGo:     stickers not painted.
    //   over, under: for "miscount", [{ colour, count }] of the colours with too
    //             many and too few.
    //   fault:    for "impossible", one of "noScheme", "centreColours",
    //             "unknownPiece", "repeatedPiece", "twistedCorner", "flippedEdge",
    //             "swappedPieces".
    //   suspect:  for "impossible", { side, index } of a sticker of the piece at
    //             fault, where one is known: the one to look at first.
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    // Whether the cube is other than it began (blank, or as the camera read it):
    // what a person would lose by leaving.
    Q_PROPERTY(bool touched READ touched NOTIFY changed)
    // The cube began as a reading, and revert() comes back to it.
    Q_PROPERTY(bool fromScan READ fromScan NOTIFY changed)
    // How many stickers the camera doubted and nobody has painted since.
    Q_PROPERTY(int doubtCount READ doubtCount NOTIFY changed)
    // The sides came from a scan that could not tell which way up each was held, so
    // each is as the camera had it and may need turning (rotateSide). True for a
    // typed-in cube, and for a scan that made a cube.
    Q_PROPERTY(bool upright READ upright NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)

public:
    // `scan`, when there is one, is where beginFromScan() takes the camera's reading
    // from.
    explicit ColourEntry(SolveController& solve, DirectScan* scan = nullptr,
                         QObject* parent = nullptr);

    [[nodiscard]] int size() const noexcept { return edit_.size(); }
    [[nodiscard]] QVariantMap sides() const;
    [[nodiscard]] QVariantMap doubts() const;
    [[nodiscard]] QVariantMap centres() const;
    [[nodiscard]] QVariantMap status() const;
    [[nodiscard]] bool touched() const { return edit_.changed(); }
    [[nodiscard]] bool fromScan() const noexcept { return edit_.from_reading(); }
    [[nodiscard]] bool upright() const noexcept { return edit_.upright(); }
    [[nodiscard]] int doubtCount() const;
    [[nodiscard]] bool canUndo() const noexcept { return edit_.can_undo(); }
    [[nodiscard]] bool canRedo() const noexcept { return edit_.can_redo(); }

    // The model, for what is not a property: the tests, and whoever fills it from
    // a reading.
    [[nodiscard]] CubeEdit& edit() noexcept { return edit_; }
    [[nodiscard]] const CubeEdit& edit() const noexcept { return edit_; }

    // Starts from what the camera read. The reading becomes where revert() goes
    // back to.
    void loadReading(Reading reading);

    // Starts from what the scan read, made a cube or not (DirectScan::reviewReading).
    // False, and nothing changed, when the scan has nothing to show.
    Q_INVOKABLE bool beginFromScan();
    // A new cube of `size` (2 to 9), nothing painted.
    Q_INVOKABLE void begin(int size);
    // Paints the sticker at `index` (row * N + col, as seen from outside) of `side`
    // with `colour` (0 to 5), or unpaints it (-1). Anything else is ignored.
    Q_INVOKABLE void paint(const QString& side, int index, int colour);
    // The same for the whole side.
    Q_INVOKABLE void paintSide(const QString& side, int colour);
    // Every sticker of every side unpainted.
    Q_INVOKABLE void clearAll();
    // How many stickers of `side` are painted.
    Q_INVOKABLE int paintedOn(const QString& side) const;

    // A side described in words: the letters W Y G B O R for its colours in reading
    // order (see parse_side). Returns { ok: true } once the side is set, or
    // { ok: false, error: "tooFew" | "tooMany" | "unknownLetter" | "noSuchSide",
    // at: n }.
    Q_INVOKABLE QVariantMap describeSide(const QString& side, const QString& text);
    // The side as describeSide would take it, a row to a line's worth ("WWW/WRW/WWW"),
    // "?" where nothing is painted: what to fill the box with.
    Q_INVOKABLE QString sideText(const QString& side) const;
    // The side turned in place by `quarterTurns` clockwise as seen from outside
    // (negative for the other way): for a reading whose sides came out the wrong
    // way up.
    Q_INVOKABLE void rotateSide(const QString& side, int quarterTurns);

    // A finger dragged over several stickers is one step to undo.
    Q_INVOKABLE void beginStroke();
    Q_INVOKABLE void endStroke();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Back to the blank cube, or to the cube the camera read.
    Q_INVOKABLE void revert();

    // Solves the cube painted, if it is one: { ok: true } once the solve has begun, or
    // { ok: false, error: "notACube" }. A cube that is not one never reaches the
    // solver.
    Q_INVOKABLE QVariantMap solve();

signals:
    void changed();

private:
    SolveController& solve_;
    DirectScan* scan_;
    CubeEdit edit_;
};

}  // namespace cube::app
