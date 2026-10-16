#pragma once

#include <qqmlintegration.h>
#include <QColor>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cube/nxn/mesh.hpp>

#include <vector>

// The stickers of a cube of any size as CubeNView draws it: what the screens
// paint (faceletColors, side letter -> N x N colours as seen from outside) in
// the order the library numbers a cube's stickers, and the turns the view has
// made since it was painted. The meshes the view draws (CubeNGeometry) are built
// from this, and so is what the tests read back (stickerPlacements).
//
// Painting it again replaces everything, so a turn the view made and the repaint
// that follows it are the same picture; the view never shows a cube that is not
// the one painted, turned by the moves it has played since.
class CubeNModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Side letter ("U", "D", "L", "R", "F", "B") -> N * N colours, each side as
    // seen from outside (cube::nxn::outside_view): top-left first, row by row. A
    // side that is missing, or has the wrong number of entries, is painted
    // `fill`. The size is read from the sides; a cube with none is `defaultSize`.
    Q_PROPERTY(QVariant faceletColors READ faceletColors WRITE setFaceletColors NOTIFY changed)
    // The colour of a sticker nothing paints, and of the plastic round them.
    Q_PROPERTY(QColor fill READ fill WRITE setFill NOTIFY changed)
    Q_PROPERTY(QColor body READ body WRITE setBody NOTIFY changed)
    Q_PROPERTY(int defaultSize READ defaultSize WRITE setDefaultSize NOTIFY changed)
    Q_PROPERTY(int size READ size NOTIFY changed)
    // Changes whenever what is drawn does: a painting, a turn.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit CubeNModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant faceletColors() const { return painting_; }
    void setFaceletColors(const QVariant& painting);
    [[nodiscard]] QColor fill() const { return fill_; }
    void setFill(const QColor& fill);
    [[nodiscard]] QColor body() const { return body_; }
    void setBody(const QColor& body);
    [[nodiscard]] int defaultSize() const noexcept { return default_size_; }
    void setDefaultSize(int size);
    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] int revision() const noexcept { return revision_; }

    // The colours the meshes are built from: one per sticker, numbered as the
    // library numbers them, in linear light (the material takes vertex colour as
    // linear), and the plastic.
    [[nodiscard]] const std::vector<cube::nxn::Rgba>& stickers() const noexcept { return linear_; }
    [[nodiscard]] const cube::nxn::Rgba& bodyColour() const noexcept { return linear_body_; }

    // Turns the layers `first` to `last` (counted from the negative end of `axis`:
    // 0 for x, 1 for y, 2 for z) `quarterTurns` quarter turns clockwise as seen
    // from the positive end of the axis. For the view to call when it has put a
    // lifted layer down turned.
    Q_INVOKABLE void turn(int axis, int first, int last, int quarterTurns);

    // Every sticker as it sits, in the cube's own frame and in cubie spacings:
    //   { color: "#rrggbb", cubie: [x, y, z], facing: [x, y, z] }
    // x runs L to R, y D to U, z B to F, and a cubie's coordinates run from
    // -(N-1)/2 to (N-1)/2. This is what the tests compare with cube::nxn::NCube.
    Q_INVOKABLE QVariantList stickerPlacements() const;

signals:
    void changed();

private:
    void rebuild();

    QVariant painting_;
    QColor fill_ = QColor::fromRgb(0xC9, 0xC5, 0xC5);
    QColor body_ = QColor::fromRgb(0x1B, 0x1A, 0x1A);
    int default_size_ = 3;
    int size_ = 3;
    int revision_ = 0;
    // The stickers in the colours they were painted, numbered as the library does.
    std::vector<QColor> colours_;
    std::vector<cube::nxn::Rgba> linear_;
    cube::nxn::Rgba linear_body_;
};
