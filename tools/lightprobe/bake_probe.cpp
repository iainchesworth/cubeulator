// bake_probe: makes the cube's light probe, src/app/qt/lightprobe/sky.ktx.
//
//   bake_probe <out.ktx> [face-size]        face-size: 128 (the default) ... 512
//
// Qt Quick 3D lights the cube with an image-based light: a sky, prefiltered
// into a cube map with a mip level per roughness. Given a plain image it does
// that prefiltering itself, on the GPU, on the first frame of every View3D -
// 512x512 faces, six levels, 1024 samples a texel - which is a few seconds on
// a software renderer, and again for every View3D. Given the *result* of that
// prefiltering as a KTX file marked QT_IBL_BAKER_VERSION it uses it as it is.
// This makes that file, once, from the same sky the cube used to make at run
// time (Qt's ProceduralSkyTextureData with its defaults), by:
//
//   1. generating the sky and writing it as an OpenEXR image;
//   2. running Qt's own baker (QSSGIblBaker, the code the run-time path is)
//      over it: 512x512 faces, six levels, RGBA16F;
//   3. averaging that down to the asked-for face size (each level keeps its
//      roughness; only the number of texels across a face drops).
//
// 128 is what shipped: the pictures the app draws with it differ from the
// run-time probe's by at most 2/255 (docs/rendering.md has the table; 512 and
// 256 are the same bit for bit). The EXR is written with its rows reversed: Qt
// reads an image file with the first row at the bottom where the procedural
// texture has it at the top, and the first bake without the reversal lit the
// underside of the cube from the sky.
//
// Qt's private headers; a project of its own (CMakeLists.txt here).

#include <QtCore/qfloat16.h>
#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <QtGui/QGuiApplication>

#include <private/proceduralskytexturedata_p.h>
#include <private/qssgiblbaker_p.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::array<unsigned char, 12> kKtxIdentifier = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                                          0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr quint32 kGlHalfFloat = 0x140B;
constexpr quint32 kGlRgba16f = 0x881A;
constexpr int kFaces = 6;

void put(QByteArray& out, const void* data, qsizetype size) {
    out.append(static_cast<const char*>(data), size);
}
void put_i32(QByteArray& out, std::int32_t value) {
    put(out, &value, 4);
}
void put_string(QByteArray& out, const char* text) {
    out.append(text, static_cast<qsizetype>(std::strlen(text)) + 1);
}

// An uncompressed, scanline OpenEXR image of half-float RGBA, written with its
// rows in reverse order (see the top of this file).
[[nodiscard]] QByteArray exr_rows_reversed(const QByteArray& rgba16f, int width, int height) {
    QByteArray out;
    const unsigned char magic[4] = {0x76, 0x2f, 0x31, 0x01};
    put(out, magic, 4);
    put_i32(out, 2);

    put_string(out, "channels");
    put_string(out, "chlist");
    QByteArray channels;
    for (const char* name : {"A", "B", "G", "R"}) {  // alphabetical, as the format requires
        put_string(channels, name);
        put_i32(channels, 1);  // HALF
        const unsigned char reserved[4] = {0, 0, 0, 0};
        put(channels, reserved, 4);
        put_i32(channels, 1);  // x sampling
        put_i32(channels, 1);  // y sampling
    }
    channels.append('\0');
    put_i32(out, static_cast<std::int32_t>(channels.size()));
    out.append(channels);

    put_string(out, "compression");
    put_string(out, "compression");
    put_i32(out, 1);
    out.append('\0');  // NO_COMPRESSION
    for (const char* name : {"dataWindow", "displayWindow"}) {
        put_string(out, name);
        put_string(out, "box2i");
        put_i32(out, 16);
        put_i32(out, 0);
        put_i32(out, 0);
        put_i32(out, width - 1);
        put_i32(out, height - 1);
    }
    put_string(out, "lineOrder");
    put_string(out, "lineOrder");
    put_i32(out, 1);
    out.append('\0');  // INCREASING_Y
    const float one = 1.0F;
    const float zeros[2] = {0.0F, 0.0F};
    put_string(out, "pixelAspectRatio");
    put_string(out, "float");
    put_i32(out, 4);
    put(out, &one, 4);
    put_string(out, "screenWindowCenter");
    put_string(out, "v2f");
    put_i32(out, 8);
    put(out, zeros, 8);
    put_string(out, "screenWindowWidth");
    put_string(out, "float");
    put_i32(out, 4);
    put(out, &one, 4);
    out.append('\0');  // end of the header

    const qsizetype line_bytes = static_cast<qsizetype>(width) * 4 * 2;
    const std::uint64_t first_line = static_cast<std::uint64_t>(out.size()) +
                                     static_cast<std::uint64_t>(height) * sizeof(std::uint64_t);
    for (int y = 0; y < height; ++y) {
        const std::uint64_t offset =
            first_line + static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(8 + line_bytes);
        put(out, &offset, 8);
    }
    const auto* source = reinterpret_cast<const std::uint16_t*>(rgba16f.constData());
    std::vector<std::uint16_t> line(static_cast<std::size_t>(width) * 4);
    for (int y = 0; y < height; ++y) {
        put_i32(out, y);
        put_i32(out, static_cast<std::int32_t>(line_bytes));
        const std::size_t row = static_cast<std::size_t>(height - 1 - y);
        for (int x = 0; x < width; ++x) {
            const std::uint16_t* pixel =
                source + (row * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                             4;  // R G B A
            // Planar per scanline, in the channels' order: A, B, G, R.
            line[static_cast<std::size_t>(x)] = pixel[3];
            line[static_cast<std::size_t>(width + x)] = pixel[2];
            line[static_cast<std::size_t>(2 * width + x)] = pixel[1];
            line[static_cast<std::size_t>(3 * width + x)] = pixel[0];
        }
        put(out, line.data(), line_bytes);
    }
    return out;
}

// A KTX1 cube map of RGBA16F: the six faces of each mip level, in order.
struct Cube {
    QByteArray key_values;
    int size = 0;  // texels across a face of level 0
    std::vector<std::array<std::vector<qfloat16>, kFaces>> levels;
};

[[nodiscard]] int level_size(int size, std::size_t level) {
    return std::max(size >> static_cast<int>(level), 1);
}

[[nodiscard]] bool read_cube(const QByteArray& file, Cube& cube) {
    if (file.size() < 64 || std::memcmp(file.constData(), kKtxIdentifier.data(), 12) != 0) {
        return false;
    }
    std::array<quint32, 13> header{};
    std::memcpy(header.data(), file.constData() + 12, sizeof(header));
    // endianness, glType, glTypeSize, glFormat, glInternalFormat,
    // glBaseInternalFormat, width, height, depth, array elements, faces,
    // levels, bytes of key/value data
    if (header[0] != 0x04030201 || header[1] != kGlHalfFloat || header[4] != kGlRgba16f ||
        header[6] != header[7] || header[10] != kFaces || header[11] == 0) {
        return false;
    }
    cube.size = static_cast<int>(header[6]);
    cube.key_values = file.mid(64, static_cast<qsizetype>(header[12]));
    qsizetype position = 64 + static_cast<qsizetype>(header[12]);
    for (std::size_t level = 0; level < header[11]; ++level) {
        quint32 image_size = 0;
        std::memcpy(&image_size, file.constData() + position, 4);
        position += 4;
        const auto texels = static_cast<std::size_t>(level_size(cube.size, level));
        if (image_size != texels * texels * 8) {
            return false;
        }
        auto& faces = cube.levels.emplace_back();
        for (auto& face : faces) {
            face.resize(texels * texels * 4);
            std::memcpy(face.data(), file.constData() + position, image_size);
            position += image_size;  // RGBA16F rows need no padding
        }
    }
    return position <= file.size();
}

[[nodiscard]] QByteArray write_cube(const Cube& cube) {
    QByteArray out(reinterpret_cast<const char*>(kKtxIdentifier.data()), 12);
    const std::array<quint32, 13> header = {0x04030201,
                                            kGlHalfFloat,
                                            2,
                                            0x1908,  // GL_RGBA
                                            kGlRgba16f,
                                            0x1908,
                                            static_cast<quint32>(cube.size),
                                            static_cast<quint32>(cube.size),
                                            0,
                                            0,
                                            kFaces,
                                            static_cast<quint32>(cube.levels.size()),
                                            static_cast<quint32>(cube.key_values.size())};
    put(out, header.data(), sizeof(header));
    out.append(cube.key_values);
    for (const auto& faces : cube.levels) {
        const auto image_size = static_cast<quint32>(faces[0].size() * sizeof(qfloat16));
        put(out, &image_size, 4);
        for (const auto& face : faces) {
            put(out, face.data(), image_size);
        }
    }
    return out;
}

// Each texel of the smaller face is the mean of a square block of the larger.
[[nodiscard]] std::vector<qfloat16> shrink(const std::vector<qfloat16>& face, int from, int to) {
    const int block = from / to;
    std::vector<qfloat16> out(static_cast<std::size_t>(to) * static_cast<std::size_t>(to) * 4);
    for (int y = 0; y < to; ++y) {
        for (int x = 0; x < to; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                float sum = 0.0F;
                for (int dy = 0; dy < block; ++dy) {
                    for (int dx = 0; dx < block; ++dx) {
                        const auto row = static_cast<std::size_t>(y * block + dy);
                        const auto column = static_cast<std::size_t>(x * block + dx);
                        sum += static_cast<float>(
                            face[(row * static_cast<std::size_t>(from) + column) * 4 +
                                 static_cast<std::size_t>(channel)]);
                    }
                }
                out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(to) +
                     static_cast<std::size_t>(x)) *
                        4 +
                    static_cast<std::size_t>(channel)] =
                    qfloat16(sum / static_cast<float>(block * block));
            }
        }
    }
    return out;
}

[[nodiscard]] Cube shrink(const Cube& cube, int size) {
    Cube out;
    out.key_values = cube.key_values;
    out.size = size;
    for (std::size_t level = 0; level < cube.levels.size(); ++level) {
        const int from = level_size(cube.size, level);
        const int to = level_size(size, level);
        auto& faces = out.levels.emplace_back();
        for (std::size_t face = 0; face < kFaces; ++face) {
            faces[face] =
                from == to ? cube.levels[level][face] : shrink(cube.levels[level][face], from, to);
        }
    }
    return out;
}

[[nodiscard]] bool is_power_of_two(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);  // the baker draws with Qt's RHI
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2 || arguments.size() > 3) {
        std::fprintf(stderr,
                     "usage: bake_probe <out.ktx> [face-size: 16 ... 512, a power of two]\n");
        return 64;
    }
    const int face_size = arguments.size() == 3 ? arguments.at(2).toInt() : 128;
    if (!is_power_of_two(face_size) || face_size > 512 || face_size < 16) {
        std::fprintf(stderr, "bake_probe: the face size must be a power of two, 16 to 512\n");
        return 64;
    }

    // 1. The sky Cube3DView.qml used to make at run time: Qt's defaults.
    ProceduralSkyTextureData sky;
    sky.generateRGBA16FTexture();
    const QByteArray texels = sky.textureData();
    const QSize size = sky.size();
    if (texels.size() != qsizetype{size.width()} * size.height() * 8) {
        std::fprintf(stderr, "bake_probe: the sky is not %dx%d RGBA16F\n", size.width(),
                     size.height());
        return 1;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::fprintf(stderr, "bake_probe: no temporary directory\n");
        return 1;
    }
    QFile exr(directory.filePath("sky.exr"));
    if (!exr.open(QIODevice::WriteOnly) ||
        exr.write(exr_rows_reversed(texels, size.width(), size.height())) < 0) {
        std::fprintf(stderr, "bake_probe: could not write the sky\n");
        return 1;
    }
    exr.close();

    // 2. Qt's baker.
    QSSGIblBaker baker;
    QStringList generated;
    const QString error = baker.import(exr.fileName(), QDir(directory.path()), &generated);
    if (!error.isEmpty() || generated.isEmpty()) {
        std::fprintf(stderr, "bake_probe: the baker failed: %s\n", qPrintable(error));
        return 1;
    }
    QFile baked(generated.first());
    if (!baked.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "bake_probe: could not read %s\n", qPrintable(generated.first()));
        return 1;
    }
    Cube cube;
    if (!read_cube(baked.readAll(), cube) || !cube.key_values.contains("QT_IBL_BAKER_VERSION")) {
        std::fprintf(stderr, "bake_probe: the baker's file is not the cube map expected\n");
        return 1;
    }

    // 3. The size it ships at.
    if (face_size < cube.size) {
        cube = shrink(cube, face_size);
    }
    QFile out(arguments.at(1));
    if (!out.open(QIODevice::WriteOnly) || out.write(write_cube(cube)) < 0) {
        std::fprintf(stderr, "bake_probe: could not write %s\n", qPrintable(arguments.at(1)));
        return 1;
    }
    std::printf("%s: %d x %d texels a face, %zu levels\n", qPrintable(arguments.at(1)), cube.size,
                cube.size, cube.levels.size());
    return 0;
}
