// cube_tumble_sim: a cube turned about one axis and then another in front of a
// camera, drawn as a recorded session - the frames, index.csv, layout.txt - so
// that cube_scan can replay a tumble whose pose is known to the degree and that
// holds no one's face.
//
//   cube_tumble_sim <out-dir> <layout> <degrees-per-second> <frames-per-second>
//                   [--yaw 360] [--pitch 360] [--tilt 8] [--blur 0]
//                   [--drift 0] [--shade 0.25] [--seed 1] [--size 3] [--moves 25]
//
// For a cube of any size, <layout> can be the word `random`: a cube of --size scrambled
// with --moves turns by --seed, written with a truth.txt (six sides as seen from outside)
// and the manifest.json that says its size, as `cube_scan replay`, `diagnose` and `board`
// read them. Few moves make a cube whose six flat sides fit more than one cube, which is
// what the corner views are for.
//
// <layout> is a cube as `cube_scan replay` prints one, "U:... D:... L:... R:...
// F:... B:..." in the solver's own layout. The camera starts square to F, the
// cube tilted `tilt` degrees; it is then turned `yaw` degrees about the
// vertical, and then `pitch` degrees about the horizontal, at the given speed,
// so every side passes the camera and every pair of neighbours for a moment
// shows two. The frames are taken at the frame rate asked for, and their times
// are in index.csv, so that a slow camera is a replay with its gaps.
//
// `blur` frames are averaged over a sixtieth of a second per picture (0 is
// none), `drift` is how far, as a fraction, a slow exposure swing takes every
// colour either way, and `shade` is how much dimmer a side is turned away from
// the light than towards it. pose.csv has each side's angle to the camera axis
// per frame, 0 being square on: it is what a reading of a frame is judged by.
//
// This is a drawing, not a camera: its light is one direction and its sides
// are flat colours with no glare, and what it shows is what the reader does
// with geometry, never with a real room.

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/outside_view.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

constexpr double kDegrees = std::numbers::pi / 180.0;

[[nodiscard]] Vec3 rotated(const Mat3& m, const Vec3& v) {
    Vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out.at(i) =
            (m.at(i).at(0) * v.at(0)) + (m.at(i).at(1) * v.at(1)) + (m.at(i).at(2) * v.at(2));
    }
    return out;
}

[[nodiscard]] Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                out.at(i).at(j) += a.at(i).at(k) * b.at(k).at(j);
            }
        }
    }
    return out;
}

[[nodiscard]] Mat3 about_x(double radians) {
    return {{{1, 0, 0},
             {0, std::cos(radians), -std::sin(radians)},
             {0, std::sin(radians), std::cos(radians)}}};
}

[[nodiscard]] Mat3 about_y(double radians) {
    return {{{std::cos(radians), 0, std::sin(radians)},
             {0, 1, 0},
             {-std::sin(radians), 0, std::cos(radians)}}};
}

[[nodiscard]] Vec3 plus(const Vec3& a, const Vec3& b, double scale = 1.0) {
    return {a.at(0) + (scale * b.at(0)), a.at(1) + (scale * b.at(1)), a.at(2) + (scale * b.at(2))};
}

[[nodiscard]] double dot(const Vec3& a, const Vec3& b) {
    return (a.at(0) * b.at(0)) + (a.at(1) * b.at(1)) + (a.at(2) * b.at(2));
}

// Sticker colours roughly as a webcam renders them in even light (BGR): the
// same as tests/unit/drawn_cube.hpp, so that a session drawn here reads as the
// unit tests' drawings do.
[[nodiscard]] cv::Scalar paint(Color colour) {
    switch (colour) {
        case Color::White:
            return {235, 235, 235};
        case Color::Yellow:
            return {40, 210, 235};
        case Color::Red:
            return {40, 40, 200};
        case Color::Orange:
            return {30, 120, 240};
        case Color::Green:
            return {70, 160, 40};
        case Color::Blue:
            return {170, 80, 20};
    }
    return {};
}

[[nodiscard]] std::optional<Color> colour_of(char letter) {
    switch (letter) {
        case 'W':
            return Color::White;
        case 'Y':
            return Color::Yellow;
        case 'R':
            return Color::Red;
        case 'O':
            return Color::Orange;
        case 'G':
            return Color::Green;
        case 'B':
            return Color::Blue;
        default:
            return std::nullopt;
    }
}

// "U:YYWYYYWWY D:YWWWWWWYY L:... R:... F:... B:..." as a cube.
[[nodiscard]] std::optional<Cube> parse_layout(std::string_view text) {
    constexpr std::array<std::pair<Face, char>, 6> kOrder = {{{Face::U, 'U'},
                                                              {Face::D, 'D'},
                                                              {Face::L, 'L'},
                                                              {Face::R, 'R'},
                                                              {Face::F, 'F'},
                                                              {Face::B, 'B'}}};
    std::array<std::array<Color, 9>, 6> faces{};
    for (const auto& [face, name] : kOrder) {
        const std::size_t at = text.find(std::string(1, name) + ":");
        if (at == std::string_view::npos || at + 2 + 9 > text.size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 9; ++i) {
            const auto colour = colour_of(text[at + 2 + i]);
            if (!colour.has_value()) {
                return std::nullopt;
            }
            faces.at(static_cast<std::size_t>(face)).at(i) = *colour;
        }
    }
    const auto cube = Cube::from_facelets(faces);
    return cube.has_value() ? std::optional<Cube>(*cube) : std::nullopt;
}

struct Camera {
    int width = 640;
    int height = 480;
    // Pinhole, on the z axis, looking at the cube's middle.
    double distance = 10.0;
    double focal = 720.0;
};

[[nodiscard]] cv::Point2f project(const Camera& camera, const Vec3& p) {
    const double depth = camera.distance - p.at(2);
    return {static_cast<float>((camera.width / 2.0) + (camera.focal * p.at(0) / depth)),
            static_cast<float>((camera.height / 2.0) - (camera.focal * p.at(1) / depth))};
}

void fill_quad(cv::Mat& frame, const Camera& camera, const std::array<Vec3, 4>& corners,
               const cv::Scalar& colour) {
    // Sixteenth-pixel positions, so that the edges are anti-aliased.
    constexpr int kShift = 4;
    std::array<cv::Point, 4> points{};
    for (std::size_t i = 0; i < 4; ++i) {
        const cv::Point2f p = project(camera, corners.at(i));
        points.at(i) = cv::Point(static_cast<int>(std::lround(p.x * 16.0F)),
                                 static_cast<int>(std::lround(p.y * 16.0F)));
    }
    cv::fillConvexPoly(frame, points.data(), 4, colour, cv::LINE_AA, kShift);
}

struct Light {
    // How much dimmer a side is when it faces away from the lamp.
    double shade = 0.25;
    // A slow swing of every colour, as a camera's own exposure makes.
    double drift = 0.0;
};

// The six sides of a cube as an outside viewer sees each, N x N stickers row-major, in
// solver::kAllFaces' order.
using Sides = std::array<std::vector<Color>, 6>;

// The cube in the pose given, as the camera sees it. It is 3 units across at any size.
[[nodiscard]] cv::Mat render(const Sides& sides, int size, const Camera& camera, const Mat3& pose,
                             const Light& light, double gain) {
    const double unit = 3.0 / static_cast<double>(size);
    cv::Mat frame(camera.height, camera.width, CV_8UC3, cv::Scalar(96, 104, 110));
    const Vec3 lamp = {-0.35, 0.55, 0.76};
    for (const Face face : cube::solver::kAllFaces) {
        const auto direction = [](Face f) {
            const auto d = cube::solver::detail::direction(f);
            return Vec3{static_cast<double>(d.at(0)), static_cast<double>(d.at(1)),
                        static_cast<double>(d.at(2))};
        };
        const Vec3 normal = rotated(pose, direction(face));
        const Vec3 up = rotated(pose, direction(cube::solver::top_of(face)));
        const Vec3 right = rotated(pose, direction(cube::solver::right_of(face)));
        const Vec3 middle = {1.5 * normal.at(0), 1.5 * normal.at(1), 1.5 * normal.at(2)};
        const Vec3 view = {-middle.at(0), -middle.at(1), camera.distance - middle.at(2)};
        if (dot(normal, view) <= 0.0) {
            continue;  // turned away: the cube is convex, so nothing of it shows
        }
        const double shade = 1.0 - (light.shade * (1.0 - std::max(0.0, dot(normal, lamp))));
        const auto quad = [&](const Vec3& centre, double half) {
            return std::array<Vec3, 4>{plus(plus(centre, right, -half), up, half),
                                       plus(plus(centre, right, half), up, half),
                                       plus(plus(centre, right, half), up, -half),
                                       plus(plus(centre, right, -half), up, -half)};
        };
        fill_quad(frame, camera, quad(middle, 1.5), cv::Scalar(22, 22, 24) * shade);
        const std::vector<Color>& stickers = sides.at(static_cast<std::size_t>(face));
        const double middle_cell = static_cast<double>(size - 1) / 2.0;
        for (int cell = 0; cell < size * size; ++cell) {
            const double column = (static_cast<double>(cell % size) - middle_cell) * unit;
            const double row = (middle_cell - static_cast<double>(cell / size)) * unit;
            const Vec3 centre = plus(plus(middle, right, column), up, row);
            fill_quad(frame, camera, quad(centre, 0.43 * unit),
                      paint(stickers.at(static_cast<std::size_t>(cell))) * (shade * gain));
        }
    }
    return frame;
}

struct Options {
    double degrees_per_second = 60.0;
    double frames_per_second = 30.0;
    double yaw = 360.0;
    double pitch = 360.0;
    double tilt = 8.0;
    int blur = 0;
    double drift = 0.0;
    double shade = 0.25;
    unsigned seed = 1;
    int size = 3;
    int moves = 25;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() < 4) {
        fmt::print(
            "usage: cube_tumble_sim <out-dir> <layout> <degrees-per-second> "
            "<frames-per-second>\n"
            "       [--yaw 360] [--pitch 360] [--tilt 8] [--blur 0] [--drift 0] [--shade 0.25] "
            "[--seed 1]\n");
        return 2;
    }
    const fs::path out = std::string(args[0]);
    Options options;
    options.degrees_per_second = std::atof(std::string(args[2]).c_str());
    options.frames_per_second = std::atof(std::string(args[3]).c_str());
    for (std::size_t i = 4; i + 1 < args.size(); i += 2) {
        const double value = std::atof(std::string(args[i + 1]).c_str());
        if (args[i] == "--yaw") {
            options.yaw = value;
        } else if (args[i] == "--pitch") {
            options.pitch = value;
        } else if (args[i] == "--tilt") {
            options.tilt = value;
        } else if (args[i] == "--blur") {
            options.blur = static_cast<int>(value);
        } else if (args[i] == "--drift") {
            options.drift = value;
        } else if (args[i] == "--shade") {
            options.shade = value;
        } else if (args[i] == "--seed") {
            options.seed = static_cast<unsigned>(value);
        } else if (args[i] == "--size") {
            options.size = static_cast<int>(value);
        } else if (args[i] == "--moves") {
            options.moves = static_cast<int>(value);
        } else {
            fmt::print("unknown option {}\n", args[i]);
            return 2;
        }
    }
    if (options.degrees_per_second <= 0.0 || options.frames_per_second <= 0.0) {
        fmt::print("the speed and the frame rate must be positive\n");
        return 2;
    }

    // The six sides to draw, and the size.
    Sides sides;
    std::string truth;
    std::string whole;
    if (args[1] == "random") {
        if (options.size < 2 || options.size > 9) {
            fmt::print("--size is 2 to 9\n");
            return 2;
        }
        const cube::nxn::NCube scrambled = cube::nxn::random_scramble(
            {.size = options.size, .seed = options.seed, .moves = options.moves});
        for (const Color colour : scrambled.facelets()) {
            whole += "WYGBOR"[static_cast<std::size_t>(colour)];
        }
        for (const Face face : cube::solver::kAllFaces) {
            sides.at(static_cast<std::size_t>(face)) = cube::nxn::outside_view(scrambled, face);
            for (const Color colour : sides.at(static_cast<std::size_t>(face))) {
                truth += "WYGBOR"[static_cast<std::size_t>(colour)];
            }
            truth += '\n';
        }
    } else {
        const auto cube = parse_layout(args[1]);
        if (!cube.has_value()) {
            fmt::print("{}\nis not a cube\n", args[1]);
            return 2;
        }
        options.size = 3;
        for (const Face face : cube::solver::kAllFaces) {
            const std::array<Color, 9> side = cube::solver::outside_view(*cube, face);
            sides.at(static_cast<std::size_t>(face)) = std::vector<Color>(side.begin(), side.end());
        }
    }

    const double omega = options.degrees_per_second * kDegrees;
    const double yaw_seconds = options.yaw * kDegrees / omega;
    const double pitch_seconds = options.pitch * kDegrees / omega;
    // Held still at each end, so that the first and last sides are read.
    constexpr double kHold = 0.6;
    const double total = kHold + yaw_seconds + pitch_seconds + kHold;
    const int frames = static_cast<int>(std::floor(total * options.frames_per_second));

    // F towards the camera, a little tilted so that no side is exactly square.
    const double tilt = options.tilt * kDegrees;
    const Mat3 start = multiply(about_x(tilt), about_y(-0.6 * tilt));
    const auto pose_at = [&](double seconds) {
        const double yawed = std::clamp(seconds - kHold, 0.0, yaw_seconds);
        const double pitched = std::clamp(seconds - kHold - yaw_seconds, 0.0, pitch_seconds);
        return multiply(about_x(omega * pitched), multiply(about_y(omega * yawed), start));
    };

    fs::create_directories(out);
    const Camera camera;
    const Light light{.shade = options.shade, .drift = options.drift};
    cv::RNG rng(options.seed);
    std::ofstream index(out / "index.csv");
    std::ofstream pose_csv(out / "pose.csv");
    index << "pass,t_ms,w,h,full_w,full_h\n";
    pose_csv << "frame,seconds,U,D,L,R,F,B\n";

    for (int i = 0; i < frames; ++i) {
        const double seconds = i / options.frames_per_second;
        const double gain = 1.0 + (light.drift * std::sin(2.0 * std::numbers::pi * seconds / 7.0));
        cv::Mat frame;
        if (options.blur > 1) {
            // Averaged over the sixtieth of a second the shutter was open.
            cv::Mat sum = cv::Mat::zeros(camera.height, camera.width, CV_32FC3);
            for (int s = 0; s < options.blur; ++s) {
                const double at = seconds - (1.0 / 120.0) +
                                  ((1.0 / 60.0) * s / static_cast<double>(options.blur - 1));
                cv::Mat shot;
                render(sides, options.size, camera, pose_at(std::max(0.0, at)), light, gain)
                    .convertTo(shot, CV_32FC3);
                sum += shot / static_cast<double>(options.blur);
            }
            sum.convertTo(frame, CV_8UC3);
        } else {
            frame = render(sides, options.size, camera, pose_at(seconds), light, gain);
        }
        cv::Mat noise(frame.size(), CV_8UC3);
        rng.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(3));
        frame += noise;
        cv::imwrite((out / fmt::format("rec_{:06d}.jpg", i)).string(), frame,
                    {cv::IMWRITE_JPEG_QUALITY, 90});

        index << i << ',' << std::llround(seconds * 1000.0) << ',' << camera.width << ','
              << camera.height << ',' << camera.width << ',' << camera.height << '\n';
        pose_csv << i << ',' << seconds;
        const Mat3 pose = pose_at(seconds);
        for (const Face face : cube::solver::kAllFaces) {
            const auto d = cube::solver::detail::direction(face);
            const Vec3 normal =
                rotated(pose, {static_cast<double>(d.at(0)), static_cast<double>(d.at(1)),
                               static_cast<double>(d.at(2))});
            const Vec3 middle = {1.5 * normal.at(0), 1.5 * normal.at(1), 1.5 * normal.at(2)};
            const Vec3 view = {-middle.at(0), -middle.at(1), camera.distance - middle.at(2)};
            const double length = std::sqrt(dot(view, view));
            pose_csv << ','
                     << std::acos(std::clamp(dot(normal, view) / length, -1.0, 1.0)) / kDegrees;
        }
        pose_csv << '\n';
    }
    std::ofstream(out / "layout.txt") << args[1] << '\n';
    if (!truth.empty()) {
        std::ofstream(out / "truth.txt") << truth;
        std::ofstream(out / "cube_n.txt") << whole << '\n';
    }
    if (options.size != 3) {
        std::ofstream(out / "manifest.json") << "{ \"size\": " << options.size << " }";
    }
    fmt::print("{} frames, {:.1f} s: {}\n", frames, total, out.string());
    return 0;
}
