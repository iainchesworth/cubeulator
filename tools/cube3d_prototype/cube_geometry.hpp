#pragma once

#include <QtQml/qqmlregistration.h>
#include <QtQuick3D/QQuick3DGeometry>

#include "cube_mesh.hpp"
#include "player.hpp"

namespace cube3d {

// The mesh of a part of the player's cube, rebuilt whenever the cube, the part
// or the lifted layer changes. See cube_mesh.hpp for what the mesh is.
class CubeGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(cube3d::Player* player READ player WRITE setPlayer NOTIFY inputChanged)
    // 0 = the whole cube, 1 = all but the lifted layer, 2 = the lifted layer.
    Q_PROPERTY(int part READ part WRITE setPart NOTIFY inputChanged)
    Q_PROPERTY(int axis READ axis WRITE setAxis NOTIFY inputChanged)
    Q_PROPERTY(int layer READ layer WRITE setLayer NOTIFY inputChanged)
    // Only an active geometry is built, and rebuilt: the one on show.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY inputChanged)
    // Bumped when the player's cube has changed, which is what an active geometry
    // that shows the cube as it stands is rebuilt on. (The lifted layer and the
    // rest are rebuilt for each move by the layer that moves, not on this.)
    Q_PROPERTY(int revision READ revision WRITE setRevision NOTIFY inputChanged)
    Q_PROPERTY(int quads READ quads NOTIFY built)
    // How long the last build took, in microseconds.
    Q_PROPERTY(double buildMicros READ buildMicros NOTIFY built)
    // The sum of every build, and how many there were.
    Q_PROPERTY(double totalBuildMicros READ totalBuildMicros NOTIFY built)
    Q_PROPERTY(int builds READ builds NOTIFY built)

public:
    explicit CubeGeometry(QQuick3DObject* parent = nullptr) : QQuick3DGeometry(parent) {}

    [[nodiscard]] Player* player() const noexcept { return player_; }
    [[nodiscard]] int part() const noexcept { return part_; }
    [[nodiscard]] int axis() const noexcept { return axis_; }
    [[nodiscard]] int layer() const noexcept { return layer_; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int revision() const noexcept { return revision_; }
    [[nodiscard]] int quads() const noexcept { return quads_; }
    [[nodiscard]] double buildMicros() const noexcept { return build_micros_; }
    [[nodiscard]] double totalBuildMicros() const noexcept { return total_micros_; }
    [[nodiscard]] int builds() const noexcept { return builds_; }

    void setPlayer(Player* player);
    void setPart(int part);
    void setAxis(int axis);
    void setLayer(int layer);
    void setActive(bool active);
    void setRevision(int revision);

signals:
    void inputChanged();
    void built();

private:
    void rebuild();

    Player* player_ = nullptr;
    int part_ = 0;
    int axis_ = 0;
    int layer_ = 0;
    bool active_ = false;
    int revision_ = 0;
    int quads_ = 0;
    double build_micros_ = 0.0;
    double total_micros_ = 0.0;
    int builds_ = 0;
};

}  // namespace cube3d
