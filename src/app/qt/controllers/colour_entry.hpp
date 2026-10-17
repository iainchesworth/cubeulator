#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <vector>

class SolveController;

namespace cube::app {

// A cube typed in by hand, a colour at a time, for a cube of any size
// (docs/design/screens/review.md, "A cube of another size").
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
    // Whether anything has been painted: what a person would lose by leaving.
    Q_PROPERTY(bool touched READ touched NOTIFY changed)

public:
    explicit ColourEntry(SolveController& solve, QObject* parent = nullptr);

    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] QVariantMap sides() const;
    [[nodiscard]] QVariantMap status() const;
    [[nodiscard]] bool touched() const;

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

    // Solves the cube painted, if it is one: { ok: true } once the solve has begun, or
    // { ok: false, error: "notACube" }.
    Q_INVOKABLE QVariantMap solve();

signals:
    void changed();

private:
    SolveController& solve_;
    int size_ = 3;
    // The stickers numbered as cube::nxn::NCube numbers them.
    std::vector<std::int8_t> colours_;
};

}  // namespace cube::app
