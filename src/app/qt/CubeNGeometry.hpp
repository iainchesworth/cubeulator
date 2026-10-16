#pragma once

#include <qqmlintegration.h>
#include <QObject>
#include <QPointer>
#include <QtQuick3D/QQuick3DGeometry>

#include "CubeNModel.hpp"

// The mesh of a part of a CubeNModel's cube, for CubeNView: all of it, or the
// layers that are lifted, or everything else. It is rebuilt when the model
// changes or the lift does, and only while it is active, so the two that show
// a lift cost nothing when no layer is in hand. See cube::nxn::build_mesh for
// what the mesh is.
class CubeNGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(CubeNModel* model READ model WRITE setModel NOTIFY inputChanged)
    // 0 = the whole cube, 1 = all but the lifted layers, 2 = the lifted layers.
    Q_PROPERTY(int part READ part WRITE setPart NOTIFY inputChanged)
    // The lifted layers: along axis (0 = x, 1 = y, 2 = z), layers first to last,
    // counted from the negative end.
    Q_PROPERTY(int axis READ axis WRITE setAxis NOTIFY inputChanged)
    Q_PROPERTY(int first READ first WRITE setFirst NOTIFY inputChanged)
    Q_PROPERTY(int last READ last WRITE setLast NOTIFY inputChanged)
    // Only an active geometry is built, and rebuilt: the one on show.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY inputChanged)
    Q_PROPERTY(int quads READ quads NOTIFY built)
    Q_PROPERTY(int builds READ builds NOTIFY built)

public:
    explicit CubeNGeometry(QQuick3DObject* parent = nullptr) : QQuick3DGeometry(parent) {}

    [[nodiscard]] CubeNModel* model() const noexcept { return model_.data(); }
    [[nodiscard]] int part() const noexcept { return part_; }
    [[nodiscard]] int axis() const noexcept { return axis_; }
    [[nodiscard]] int first() const noexcept { return first_; }
    [[nodiscard]] int last() const noexcept { return last_; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int quads() const noexcept { return quads_; }
    [[nodiscard]] int builds() const noexcept { return builds_; }

    void setModel(CubeNModel* model);
    void setPart(int part);
    void setAxis(int axis);
    void setFirst(int first);
    void setLast(int last);
    void setActive(bool active);

signals:
    void inputChanged();
    void built();

private:
    void rebuild();

    QPointer<CubeNModel> model_;
    int part_ = 0;
    int axis_ = 0;
    int first_ = 0;
    int last_ = 0;
    bool active_ = false;
    int quads_ = 0;
    int builds_ = 0;
};
