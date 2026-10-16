#include "CubeNGeometry.hpp"

#include <cube/nxn/mesh.hpp>

#include <QByteArray>
#include <QVector3D>

#include <algorithm>
#include <cstdint>
#include <cstring>

void CubeNGeometry::setModel(CubeNModel* model) {
    if (model == model_.data()) {
        return;
    }
    if (model_ != nullptr) {
        disconnect(model_, &CubeNModel::changed, this, &CubeNGeometry::rebuild);
    }
    model_ = model;
    if (model_ != nullptr) {
        connect(model_, &CubeNModel::changed, this, &CubeNGeometry::rebuild);
    }
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::setPart(int part) {
    if (part == part_) {
        return;
    }
    part_ = part;
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::setAxis(int axis) {
    if (axis == axis_) {
        return;
    }
    axis_ = axis;
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::setFirst(int first) {
    if (first == first_) {
        return;
    }
    first_ = first;
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::setLast(int last) {
    if (last == last_) {
        return;
    }
    last_ = last;
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::setActive(bool active) {
    if (active == active_) {
        return;
    }
    active_ = active;
    emit inputChanged();
    rebuild();
}

void CubeNGeometry::rebuild() {
    if (model_ == nullptr || !active_) {
        return;
    }
    const int size = model_->size();
    const cube::nxn::LiftedLayers lifted{
        .axis = static_cast<cube::nxn::Axis>(std::clamp(axis_, 0, 2)),
        .first = first_,
        .last = last_,
    };
    const cube::nxn::Mesh mesh =
        cube::nxn::build_mesh(size, model_->stickers(), model_->bodyColour(),
                              static_cast<cube::nxn::MeshPart>(part_), lifted);

    clear();
    const auto vertex_bytes = static_cast<qsizetype>(mesh.vertices.size() * sizeof(float));
    const auto index_bytes = static_cast<qsizetype>(mesh.indices.size() * sizeof(std::uint32_t));
    QByteArray vertices(vertex_bytes, Qt::Uninitialized);
    QByteArray indices(index_bytes, Qt::Uninitialized);
    std::memcpy(vertices.data(), mesh.vertices.data(), static_cast<std::size_t>(vertex_bytes));
    std::memcpy(indices.data(), mesh.indices.data(), static_cast<std::size_t>(index_bytes));
    setVertexData(vertices);
    setIndexData(indices);
    setStride(static_cast<int>(cube::nxn::Mesh::kFloatsPerVertex * sizeof(float)));
    setPrimitiveType(PrimitiveType::Triangles);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, 3 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, 6 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);

    const float reach = (static_cast<float>(size) / 2.0F) + 0.1F;
    setBounds(QVector3D(-reach, -reach, -reach), QVector3D(reach, reach, reach));
    update();

    quads_ = static_cast<int>(mesh.quads());
    ++builds_;
    emit built();
}
