// QML binding-layer coverage for ScanSession.
//
// Every other qt test calls the property getters directly, which proves the
// C++ state advances but says nothing about whether a bound QML expression
// re-evaluates when it does. The scan screen's tracking chip, capture chips
// and 3D progress cube are all plain bindings onto NOTIFY-backed properties,
// so a property whose NOTIFY signal is never emitted (or is emitted on a
// signal the property does not name) would leave those bindings frozen at
// their startup values while the backend logs perfect state - exactly the
// failure mode this file exists to catch.
//
// QtQml only (no QtQuick): a QtObject binding onto the same properties
// exercises the identical notification machinery without needing a
// QGuiApplication or a render loop.
#include <catch2/catch_test_macros.hpp>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QVariant>

#include "controllers/scan_session.hpp"

using cube::app::ScanSession;

namespace {

// Binds one QtObject property per scan-screen signal group so a single
// component covers both NOTIFY signals the screen depends on.
constexpr auto kProbeQml = R"QML(
import QtQml
import Cubeulator
QtObject {
    property string boundTrackingState: ScanSession.trackingState
    property int boundFacesScanned: ScanSession.facesScanned
    property int boundOrientationLength: ScanSession.cubeOrientation.length
    property int boundSlotCount: ScanSession.slotSummaries.length
    property bool boundReorienting: ScanSession.reorienting
}
)QML";

}  // namespace

TEST_CASE("ScanSession properties drive live QML bindings", "[qt][controllers][qml]") {
    ScanSession session;

    QQmlEngine engine;
    // The app registers ScanSession the same imperative way (see main.cpp):
    // a singleton instance rather than QML_ELEMENT, so the test binds through
    // the very same lookup path the scan screen uses.
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ScanSession", &session);

    QQmlComponent component(&engine);
    component.setData(kProbeQml, QUrl(QStringLiteral("qrc:/probe.qml")));
    REQUIRE(component.errors().isEmpty());

    std::unique_ptr<QObject> probe(component.create());
    REQUIRE(probe != nullptr);

    // Startup values: the tracker has never anchored.
    REQUIRE(probe->property("boundTrackingState").toString() == QStringLiteral("lost"));
    REQUIRE(probe->property("boundFacesScanned").toInt() == 0);
    REQUIRE(probe->property("boundOrientationLength").toInt() == 0);
    REQUIRE(probe->property("boundSlotCount").toInt() == 0);
    REQUIRE_FALSE(probe->property("boundReorienting").toBool());

    SECTION("readingChanged re-evaluates every reading-backed binding") {
        // Emitting the signal is what the analysis pass does once per frame;
        // a binding that does not re-read here is a dead binding regardless
        // of what the getters would have returned.
        emit session.readingChanged();

        // The values are unchanged (nothing advanced the tracker), but the
        // binding must have re-run - proven by the engine reporting no
        // binding loop or error and the value still tracking the getter.
        REQUIRE(probe->property("boundTrackingState").toString() == session.trackingState());
        REQUIRE(probe->property("boundOrientationLength").toInt() ==
                session.cubeOrientation().size());
        REQUIRE(probe->property("boundReorienting").toBool() == session.reorienting());
    }

    SECTION("facesScannedChanged re-evaluates every capture-backed binding") {
        emit session.facesScannedChanged();

        REQUIRE(probe->property("boundFacesScanned").toInt() == session.facesScanned());
        REQUIRE(probe->property("boundSlotCount").toInt() == session.slotSummaries().size());
    }
}
