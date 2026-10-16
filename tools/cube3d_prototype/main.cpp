// cube3d_prototype: the layer-lift renderer for a cube of any size, and the way a
// 3x3's view would scale if it were simply built larger, side by side, so that
// what each costs can be measured. See README.md.
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QVariantMap>

#include "cube_mesh.hpp"
#include "frame_stats.hpp"
#include "player.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace {

// The mesh against the library, at every size: the stickers a layer turn moves
// are those on the cubies the mesh lifts, and the meshes have the quads they
// should. Returns the number of things wrong.
int selftest() {
    int wrong = 0;
    const auto fail = [&wrong](const char* what, int size, int axis, int layer) {
        std::printf("FAIL size %d axis %d layer %d: %s\n", size, axis, layer, what);
        ++wrong;
    };
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(size);
        const auto cube = cube::nxn::NCube::solved(size);
        const cube3d::Palette palette = cube3d::default_palette();
        const std::size_t whole = cube3d::build_mesh(*cube, cube3d::Part::Whole, 0, 0, palette).quads();
        if (whole != static_cast<std::size_t>(12 * size * size)) {
            fail("the whole cube is not 12 N^2 quads", size, 0, 0);
        }
        for (int axis = 0; axis < 3; ++axis) {
            for (int layer = 0; layer < size; ++layer) {
                // The facelets in the lifted layer, by the mesh's own idea of where they are.
                std::set<std::size_t> in_layer;
                for (const cube::solver::Face face : cube::solver::kAllFaces) {
                    for (int row = 0; row < size; ++row) {
                        for (int col = 0; col < size; ++col) {
                            const auto at = cube3d::place_of(size, face, row, col);
                            if (at.cubie.at(static_cast<std::size_t>(axis)) == layer) {
                                in_layer.insert(geometry.index(face, row, col));
                            }
                        }
                    }
                }
                // The facelets the library's turn of that layer moves.
                const cube::nxn::SliceTurn turn{
                    .axis = static_cast<cube::nxn::Axis>(axis), .layer = layer, .turns = 1};
                std::set<std::size_t> moved;
                for (const std::uint16_t f : cube::nxn::support(geometry.turn(turn))) {
                    moved.insert(f);
                }
                if (!std::ranges::includes(in_layer, moved)) {
                    fail("a turn moves a sticker the mesh does not lift", size, axis, layer);
                }
                // What the lift leaves where it is: only the middle of the face a turn is about.
                if (in_layer.size() - moved.size() > 1) {
                    fail("the mesh lifts stickers the turn leaves alone", size, axis, layer);
                }
                const std::size_t layer_quads =
                    cube3d::build_mesh(*cube, cube3d::Part::Layer, axis, layer, palette).quads();
                const std::size_t rest_quads =
                    cube3d::build_mesh(*cube, cube3d::Part::Rest, axis, layer, palette).quads();
                const bool outer = layer == 0 || layer == size - 1;
                const std::size_t cuts = outer ? 2U : 4U;
                if (layer_quads + rest_quads !=
                    whole + (cuts * static_cast<std::size_t>(size * size))) {
                    fail("the layer and the rest do not make the cube and its cuts", size, axis, layer);
                }
            }
        }
        std::printf("size %d: %zu quads whole\n", size, whole);
    }
    if (wrong == 0) {
        std::printf("selftest ok\n");
    } else {
        std::printf("selftest FAILED: %d\n", wrong);
    }
    return wrong == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // Pinned before Qt reads them, unless asked otherwise: the basic loop renders on
    // the GUI thread, and with vsync off a frame is as long as it takes, which is
    // what a measurement is for (the system tests do the same).
    bool vsync = false;
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--vsync") {
            vsync = true;
        }
    }
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "basic");
    }
    if (!vsync && !qEnvironmentVariableIsSet("QSG_NO_VSYNC")) {
        qputenv("QSG_NO_VSYNC", "1");
    }

    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--selftest") {
            return selftest();
        }
    }
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"mode", "lift (the layer-lift renderer) or naive (one object per cubie)", "mode",
                      "lift"});
    parser.addOption({"size", "cube size, 2 to 9", "n", "5"});
    parser.addOption({"moves", "moves to play (0 = the whole solution with --solve)", "n", "40"});
    parser.addOption({"turn-ms", "how long a quarter turn takes", "ms", "160"});
    parser.addOption({"idle-ms", "how long to watch the still cube first", "ms", "1500"});
    parser.addOption({"seed", "scramble seed", "n", "1"});
    parser.addOption({"report", "append the result, as a line of JSON, to this file", "file", ""});
    parser.addOption({"screenshot", "save a picture of the cube after the moves", "file", ""});
    parser.addOption({"screenshot-mid", "save a picture part-way through the fourth move", "file", ""});
    parser.addOption({"solve", "play a real solution to the scrambled cube"});
    parser.addOption({"vsync", "leave vsync on"});
    parser.process(app);

    const int size = parser.value("size").toInt();
    cube3d::Player player(size, parser.value("seed").toUInt(), parser.value("moves").toInt(),
                          parser.isSet("solve"));
    cube3d::FrameStats stats;
    stats.setReportPath(parser.value("report"));

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"player", QVariant::fromValue(&player)},
        {"stats", QVariant::fromValue(&stats)},
        {"mode", parser.value("mode")},
        {"turnMs", parser.value("turn-ms").toInt()},
        {"idleMs", parser.value("idle-ms").toInt()},
        {"screenshot", parser.value("screenshot")},
        {"screenshotMid", parser.value("screenshot-mid")},
        {"solving", parser.isSet("solve")},
    });
    engine.loadFromModule("CubePrototype", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return QGuiApplication::exec();
}
