#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>

#include <emscripten.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QMetaProperty>
#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWindow>

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>

// A read-only view of the running app for the Playwright suite (tests/web).
//
// The native system tests find a control by objectName, read what a person
// sees from the item tree and the singletons, and press with mouse events
// (tests/system/app.hpp). A browser test cannot reach into Qt's items, and Qt
// for WebAssembly does not mirror them into the DOM's accessibility tree (the
// page holds one hidden button), so this is the same window onto the app: the
// named items with where they are, the text a person can read, the singletons'
// properties, and the six sides of a scrambled cube as the camera should be
// shown them. Pressing is not here on purpose: the test sends real pointer and
// key events to the page, which is the path a person's takes.
//
// It only reads, and it is compiled in only where CMake asks for it
// (CUBEULATOR_WEB_TEST_DRIVER, web-emscripten-diagnostics): the page that is
// published carries none of it. driver_web.js is the page's half and defines
// `window.cubeulatorDriver`.

// Provided by driver_web.js.
extern "C" {
void cube_web_driver_install();
}

namespace {

using cube::solver::Color;
using cube::solver::Face;

// One string at a time, valid until the next call: the page copies it out at
// once, on the main thread.
std::mutex g_text_mutex;
std::string g_text;

const char* hold(const QJsonValue& value) {
    QByteArray bytes;
    if (value.isObject()) {
        bytes = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    } else if (value.isArray()) {
        bytes = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    } else {
        bytes = QByteArrayLiteral("null");
    }
    const std::scoped_lock lock(g_text_mutex);
    g_text.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    return g_text.c_str();
}

QQuickWindow* main_window() {
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto* quick = qobject_cast<QQuickWindow*>(window)) {
            return quick;
        }
    }
    return nullptr;
}

// The engine that loaded the app. The window and the items Qt Quick Controls
// make for it are made from C++ and have none; the app's own items, made from
// its QML, do.
QQmlEngine* engine_of_item(const QQuickItem* item) {
    if (QQmlEngine* engine = qmlEngine(item)) {
        return engine;
    }
    for (const QQuickItem* child : item->childItems()) {
        if (QQmlEngine* engine = engine_of_item(child)) {
            return engine;
        }
    }
    return nullptr;
}

QQmlEngine* engine_of(QQuickWindow* window) {
    if (window == nullptr) {
        return nullptr;
    }
    if (QQmlEngine* engine = qmlEngine(window)) {
        return engine;
    }
    return engine_of_item(window->contentItem());
}

QObject* singleton(QQmlEngine* engine, const QString& name) {
    if (engine == nullptr) {
        return nullptr;
    }
    return engine->singletonInstance<QObject*>("Cubeulator", name.toLatin1().constData());
}

// Every readable property that has a JSON form. A colour becomes "#rrggbb"; an
// object, which has none, is left out rather than turned into "null". An
// enumeration is its number: a test that wants a name reads the property that
// is a string.
QJsonObject properties_of(const QObject* object) {
    QJsonObject out;
    const QMetaObject* meta = object->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (!property.isReadable()) {
            continue;
        }
        const QVariant value = property.read(object);
        if (!value.isValid() || value.canConvert<QObject*>()) {
            continue;
        }
        const QString name = QString::fromLatin1(property.name());
        if (name == QLatin1String("objectName")) {
            continue;
        }
        out.insert(name, QJsonValue::fromVariant(value));
    }
    return out;
}

// What the app's own screen items expose that a test asserts on. Only those a
// given item has are read.
constexpr std::array<const char*, 11> kItemProperties = {
    "text", "checked", "value", "currentIndex", "currentText", "from",
    "to",   "activeFocus", "source", "selected", "state"};

struct Chain {
    bool visible = true;
    bool enabled = true;
    double opacity = 1.0;
};

void walk(const QQuickItem* item, const Chain& parent, QJsonArray& out) {
    Chain here;
    here.visible = parent.visible && item->isVisible();
    here.enabled = parent.enabled && item->isEnabled();
    here.opacity = parent.opacity * item->opacity();

    const QString name = item->objectName();
    const QVariant text = item->property("text");
    const bool has_text = text.isValid() && text.typeId() == QMetaType::QString &&
                          !text.toString().isEmpty();
    if (!name.isEmpty() || has_text) {
        const QPointF origin = item->mapToScene(QPointF(0, 0));
        QJsonObject entry;
        entry.insert("name", name);
        entry.insert("type", QString::fromLatin1(item->metaObject()->className()));
        entry.insert("x", origin.x());
        entry.insert("y", origin.y());
        entry.insert("width", item->width());
        entry.insert("height", item->height());
        entry.insert("visible", here.visible);
        entry.insert("enabled", here.enabled);
        entry.insert("opacity", here.opacity);
        const QMetaObject* meta = item->metaObject();
        for (const char* property : kItemProperties) {
            if (meta->indexOfProperty(property) < 0) {
                continue;
            }
            const QVariant value = item->property(property);
            if (value.isValid()) {
                entry.insert(QString::fromLatin1(property), QJsonValue::fromVariant(value));
            }
        }
        out.append(entry);
    }
    for (const QQuickItem* child : item->childItems()) {
        walk(child, here, out);
    }
}

QString color_name(Color color) {
    switch (color) {
        case Color::White:
            return QStringLiteral("white");
        case Color::Yellow:
            return QStringLiteral("yellow");
        case Color::Green:
            return QStringLiteral("green");
        case Color::Blue:
            return QStringLiteral("blue");
        case Color::Orange:
            return QStringLiteral("orange");
        case Color::Red:
            return QStringLiteral("red");
    }
    return {};
}

cube::model::FacetColor facet_color(Color color) {
    switch (color) {
        case Color::White:
            return cube::model::FacetColor::white;
        case Color::Yellow:
            return cube::model::FacetColor::yellow;
        case Color::Green:
            return cube::model::FacetColor::green;
        case Color::Blue:
            return cube::model::FacetColor::blue;
        case Color::Orange:
            return cube::model::FacetColor::orange;
        case Color::Red:
            return cube::model::FacetColor::red;
    }
    return cube::model::FacetColor::unknown;
}

QString face_name(Face face) {
    switch (face) {
        case Face::U:
            return QStringLiteral("U");
        case Face::D:
            return QStringLiteral("D");
        case Face::L:
            return QStringLiteral("L");
        case Face::R:
            return QStringLiteral("R");
        case Face::F:
            return QStringLiteral("F");
        case Face::B:
            return QStringLiteral("B");
    }
    return {};
}

// A side of `cube` as a camera pointed at it sees it: the same view
// tests/unit/drawn_cube.hpp's outside_view gives the native scan tests. The
// solver's array is camera-handed on U, F and L and mirror-handed on D, R and
// B, so the other three are column-flipped.
std::array<Color, 9> camera_view(const cube::solver::Cube& cube, Face face) {
    std::array<Color, 9> stickers = cube.face(face);
    if (face == Face::D || face == Face::R || face == Face::B) {
        for (std::size_t row = 0; row < 3; ++row) {
            std::swap(stickers[row * 3], stickers[row * 3 + 2]);
        }
    }
    return stickers;
}

// Six sides, each square-on and in any roll, make exactly one cube - which
// about one scramble in eighty does not (drawn_cube.hpp, scramble()): those
// sides can be put together two ways and the scan reports them as unreadable.
// A test of the scan wants a cube the scan can read.
bool sides_make_one_cube(const cube::solver::Cube& cube) {
    std::array<std::array<cube::model::Facelet, 9>, 6> faces{};
    std::size_t index = 0;
    for (const Face face : cube::solver::kAllFaces) {
        const std::array<Color, 9> side = camera_view(cube, face);
        for (std::size_t i = 0; i < 9; ++i) {
            faces[index][i] =
                cube::model::Facelet{.color = facet_color(side[i]), .confidence = 1.0F};
        }
        ++index;
    }
    return cube::solver::assemble_scanned_faces(faces).has_value();
}

}  // namespace

extern "C" {

// { screen, window: {width, height, devicePixelRatio}, items: [...] } - or
// { window: null } before the window exists. `items` is every item that has an
// objectName or text, in paint order (later paints over earlier), each with its
// scene rectangle and whether a person can see and press it.
EMSCRIPTEN_KEEPALIVE const char* cube_web_driver_state() {
    QJsonObject state;
    QQuickWindow* window = main_window();
    if (window == nullptr) {
        state.insert("window", QJsonValue::Null);
        return hold(state);
    }

    QJsonObject size;
    size.insert("width", window->width());
    size.insert("height", window->height());
    size.insert("devicePixelRatio", window->devicePixelRatio());
    state.insert("window", size);

    QJsonArray items;
    walk(window->contentItem(), Chain{}, items);
    state.insert("items", items);

    QObject* app_state = singleton(engine_of(window), QStringLiteral("AppState"));
    state.insert("screen",
                 app_state != nullptr ? app_state->property("screen").toString() : QString());
    return hold(state);
}

// The properties of one of the "Cubeulator" module's singletons (AppState,
// SettingsState, Theme, SolveController, DirectScan, CameraController, ...) as
// an object, or null if there is none by that name.
EMSCRIPTEN_KEEPALIVE const char* cube_web_driver_singleton(const char* name) {
    QObject* object = singleton(engine_of(main_window()), QString::fromUtf8(name));
    return hold(object != nullptr ? QJsonValue(properties_of(object)) : QJsonValue());
}

// { seed, faces: [{face, stickers}] }: the six sides of a scrambled cube, in the
// order the app's own scan tests show them, as colour names, nine row-major
// from the top-left as the camera sees each. `seed` is the first one the scan
// can read at all, at or above the one asked for.
EMSCRIPTEN_KEEPALIVE const char* cube_web_driver_sides(unsigned seed, int moves) {
    unsigned used = seed;
    cube::solver::Cube cube = cube::solver::random_scramble(used, cube::solver::MoveCount{moves});
    while (!sides_make_one_cube(cube)) {
        cube = cube::solver::random_scramble(++used, cube::solver::MoveCount{moves});
    }

    QJsonArray faces;
    for (const Face face : cube::solver::kAllFaces) {
        QJsonArray stickers;
        for (const Color color : camera_view(cube, face)) {
            stickers.append(color_name(color));
        }
        QJsonObject entry;
        entry.insert("face", face_name(face));
        entry.insert("stickers", stickers);
        faces.append(entry);
    }
    QJsonObject out;
    out.insert("seed", static_cast<double>(used));
    out.insert("faces", faces);
    return hold(out);
}

}  // extern "C"

namespace {

struct Installer {
    Installer() { cube_web_driver_install(); }
};
const Installer installer;

}  // namespace
