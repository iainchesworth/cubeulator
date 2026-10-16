#include "cube_geometry.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QVector3D>

#include <algorithm>
#include <cstring>

namespace cube3d {

void CubeGeometry::setPlayer(Player* player) {
    if (player == player_) {
        return;
    }
    player_ = player;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::setPart(int part) {
    if (part == part_) {
        return;
    }
    part_ = part;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::setAxis(int axis) {
    if (axis == axis_) {
        return;
    }
    axis_ = axis;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::setLayer(int layer) {
    if (layer == layer_) {
        return;
    }
    layer_ = layer;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::setRevision(int revision) {
    if (revision == revision_) {
        return;
    }
    revision_ = revision;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::setActive(bool active) {
    if (active == active_) {
        return;
    }
    active_ = active;
    emit inputChanged();
    rebuild();
}

void CubeGeometry::rebuild() {
    if (player_ == nullptr || !active_) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    static const Palette palette = default_palette();
    const Mesh mesh = build_mesh(player_->cube(), static_cast<Part>(part_), axis_, layer_, palette);

    clear();
    const auto vertex_bytes = static_cast<qsizetype>(mesh.vertices.size() * sizeof(float));
    const auto index_bytes = static_cast<qsizetype>(mesh.indices.size() * sizeof(std::uint32_t));
    QByteArray vertices(vertex_bytes, Qt::Uninitialized);
    QByteArray indices(index_bytes, Qt::Uninitialized);
    std::memcpy(vertices.data(), mesh.vertices.data(), static_cast<std::size_t>(vertex_bytes));
    std::memcpy(indices.data(), mesh.indices.data(), static_cast<std::size_t>(index_bytes));
    setVertexData(vertices);
    setIndexData(indices);
    setStride(static_cast<int>(Mesh::kFloatsPerVertex * sizeof(float)));
    setPrimitiveType(PrimitiveType::Triangles);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, 3 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, 6 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);

    const float reach = static_cast<float>(player_->size()) / 2.0F + 0.1F;
    setBounds(QVector3D(-reach, -reach, -reach), QVector3D(reach, reach, reach));
    update();

    quads_ = static_cast<int>(mesh.quads());
    build_micros_ = static_cast<double>(timer.nsecsElapsed()) / 1000.0;
    total_micros_ += build_micros_;
    ++builds_;
    emit built();
}

}  // namespace cube3d
