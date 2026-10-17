// cube_scan_s0: gate S0 of docs/nxn-app-plan.md. How well the direct-scan
// reader does on a cube of N x N, measured offline on cubes that are drawn
// (tests/unit/drawn_cube.hpp) because no recording of one exists yet.
//
//   cube_scan_s0 [--sizes 3,5,7] [--pitches 14,18,24,32,40] [--cubes 40]
//                [--light daylight|warm|both] [--noise 3] [--jitter 0.5]
//                [--blur 0] [--frames 12] [--seed 1] [--threads N] [--csv]
//
// For each size and each sticker spacing (pixels, in a 640 x 360 frame: a 3x3
// held as a person holds one is about 27) it scrambles `cubes` cubes, draws
// their six sides one after another the way a person shows them - each in its
// own place, turned a little in its plane, in a random quarter-turn, held for
// `frames` frames that shake by `jitter` pixels - and runs the reader and the
// scan over the frames. It reports
//
//   read     the share of frames in which the side was found at all
//   sticker  the share of stickers of those reads whose colour is the nearest
//            of the six paints to the one drawn (the reader's geometry and
//            sampling, without the naming)
//   named    the share of the 6 N^2 stickers the scan named correctly, among
//            scans that captured six sides (the whole path to a colour)
//   exact / ambiguous / wrong / none / short
//            how the scans came out: the true cube; several cubes (six flat
//            sides do not always say which: inherent, not a misread); a cube
//            that is NOT the one drawn (a false accept); no cube; fewer than six
//            sides captured
//
// This is a drawing, not a camera: its stickers are flat colours with a little
// noise, and what it measures is what the reader does with geometry and
// resolution, never with a real room, a real lamp or a real hand.

#include "drawn_cube.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/vision/direct_scan.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using cube::nxn::NCube;
using cube::solver::Color;
using cube::solver::Face;

struct Options {
    std::vector<int> sizes = {3, 4, 5, 7};
    std::vector<float> pitches = {12, 16, 20, 24, 28, 34, 40};
    // The cube's extent across the frame's height, as a share of it: the pitch
    // of a size is then extent * height / size. When given it replaces pitches.
    std::vector<float> extents;
    int cubes = 30;
    std::vector<std::string> lights = {"daylight", "warm"};
    float noise = 3.0F;
    float jitter = 0.5F;
    float blur = 0.0F;
    int frames = 12;
    unsigned seed = 1;
    unsigned threads = 0;
    bool csv = false;
    bool verbose = false;
    std::string dump;
    cv::Size frame{640, 360};
};

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        parts.push_back(part);
    }
    return parts;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--sizes") {
            options.sizes.clear();
            for (const auto& part : split(next())) {
                options.sizes.push_back(std::stoi(part));
            }
        } else if (arg == "--pitches") {
            options.pitches.clear();
            for (const auto& part : split(next())) {
                options.pitches.push_back(std::stof(part));
            }
        } else if (arg == "--extents") {
            options.extents.clear();
            for (const auto& part : split(next())) {
                options.extents.push_back(std::stof(part));
            }
        } else if (arg == "--cubes") {
            options.cubes = std::stoi(next());
        } else if (arg == "--light") {
            const std::string light = next();
            options.lights = light == "both" ? std::vector<std::string>{"daylight", "warm"}
                                             : std::vector<std::string>{light};
        } else if (arg == "--noise") {
            options.noise = std::stof(next());
        } else if (arg == "--jitter") {
            options.jitter = std::stof(next());
        } else if (arg == "--blur") {
            options.blur = std::stof(next());
        } else if (arg == "--frames") {
            options.frames = std::stoi(next());
        } else if (arg == "--seed") {
            options.seed = static_cast<unsigned>(std::stoul(next()));
        } else if (arg == "--threads") {
            options.threads = static_cast<unsigned>(std::stoul(next()));
        } else if (arg == "--csv") {
            options.csv = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--frame") {
            const std::string text = next();
            const auto x = text.find('x');
            options.frame = {std::stoi(text.substr(0, x)), std::stoi(text.substr(x + 1))};
        } else if (arg == "--dump") {
            options.dump = next();
        }
    }
    return options;
}

// What one batch of cubes came to.
struct Tally {
    long frames = 0;
    long frames_read = 0;
    long stickers = 0;
    long stickers_right = 0;
    long scans = 0;
    long scans_six = 0;
    long named = 0;
    long named_right = 0;
    long exact = 0;
    long ambiguous = 0;
    long wrong = 0;
    long none = 0;
    long short_of_six = 0;
    long counted_reads = 0;   // reads the steadiness gate let through (sum over scans)
    long wanted_reads = 0;    // reads that were there to count
    long found_stickers = 0;  // stickers the reader found as blobs, of the reads
    long lattice_stickers = 0;
    long side_frames_read = 0;     // frames of a held side that were read, and how many of
    long side_frames_counted = 0;  // those the steadiness gate counted by the end of a hold

    void add(const Tally& other) {
        frames += other.frames;
        frames_read += other.frames_read;
        stickers += other.stickers;
        stickers_right += other.stickers_right;
        scans += other.scans;
        scans_six += other.scans_six;
        named += other.named;
        named_right += other.named_right;
        exact += other.exact;
        ambiguous += other.ambiguous;
        wrong += other.wrong;
        none += other.none;
        short_of_six += other.short_of_six;
        counted_reads += other.counted_reads;
        wanted_reads += other.wanted_reads;
        found_stickers += other.found_stickers;
        lattice_stickers += other.lattice_stickers;
        side_frames_read += other.side_frames_read;
        side_frames_counted += other.side_frames_counted;
    }
};

// The six paints as Lab, for judging a read sticker by the nearest.
std::array<cv::Vec3f, 6> paints_lab(const cube::test::Drawing& drawing) {
    std::array<cv::Vec3f, 6> labs{};
    for (const Color colour :
         {Color::White, Color::Yellow, Color::Green, Color::Blue, Color::Orange, Color::Red}) {
        const cv::Scalar bgr = drawing.paint(colour);
        cv::Mat3f pixel(
            1, 1,
            cv::Vec3f(static_cast<float>(bgr[0]) / 255.0F, static_cast<float>(bgr[1]) / 255.0F,
                      static_cast<float>(bgr[2]) / 255.0F));
        cv::Mat3f lab;
        cv::cvtColor(pixel, lab, cv::COLOR_BGR2Lab);
        labs[static_cast<std::size_t>(colour)] = lab(0, 0);
    }
    return labs;
}

Color nearest_paint(const std::array<cv::Vec3f, 6>& paints, const cv::Vec3f& lab) {
    std::size_t best = 0;
    float best_distance = 1.0e9F;
    for (std::size_t i = 0; i < paints.size(); ++i) {
        const float distance = cube::vision::colour_distance(lab, paints[i]);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return static_cast<Color>(best);
}

// A quarter-turn of an N x N side, as drawn_cube.hpp's turned_clockwise.
std::vector<Color> turned(std::vector<Color> side, int quarter_turns) {
    for (int i = 0; i < quarter_turns; ++i) {
        side = cube::test::turned_clockwise(side);
    }
    return side;
}

bool holds_cube(const NCube& made, const NCube& truth) {
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(truth.size());
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        NCube held = truth;
        held.apply(geometry.rotation_permutation(rotation));
        if (held == made) {
            return true;
        }
    }
    return false;
}

struct Scene {
    int size = 3;
    float pitch = 28.0F;
    std::string light = "daylight";
    const Options* options = nullptr;
};

Tally run_cube(const Scene& scene, unsigned seed) {
    Tally tally;
    std::mt19937 rng(seed);
    const auto uniform = [&rng](float low, float high) {
        return std::uniform_real_distribution<float>(low, high)(rng);
    };
    const Options& options = *scene.options;
    cube::test::Drawing drawing =
        scene.light == "warm" ? cube::test::warm_lamp_drawing() : cube::test::Drawing{};
    drawing.size = options.frame;
    const std::array<cv::Vec3f, 6> paints = paints_lab(drawing);

    const NCube truth =
        cube::nxn::random_scramble({.size = scene.size, .seed = seed, .moves = 20 * scene.size});
    std::array<std::size_t, 6> order = {0, 1, 2, 3, 4, 5};
    std::ranges::shuffle(order, rng);

    cube::vision::SideScan scan(scene.size);
    std::vector<int> found_per_side;
    std::vector<int> counted_per_side;
    // What was drawn, in the order the sides were first shown, for judging the
    // scan's naming.
    std::vector<std::vector<Color>> shown;
    for (const std::size_t which : order) {
        const Face face = cube::solver::kAllFaces.at(which);
        std::vector<Color> stickers =
            turned(cube::nxn::outside_view(truth, face), static_cast<int>(rng() % 4));
        shown.push_back(stickers);
        cube::test::Pose pose;
        pose.pitch = scene.pitch;
        const float half_width = static_cast<float>(options.frame.width) / 2.0F;
        const float half_height = static_cast<float>(options.frame.height) / 2.0F;
        pose.centre = {
            half_width + uniform(-0.11F, 0.11F) * static_cast<float>(options.frame.width),
            half_height + uniform(-0.11F, 0.11F) * static_cast<float>(options.frame.height)};
        pose.degrees = uniform(-20.0F, 20.0F);
        pose.squash = uniform(0.92F, 1.0F);
        int found_here = 0;
        std::string trace;
        for (int frame = 0; frame < options.frames; ++frame) {
            cube::test::Pose shaken = pose;
            shaken.centre += cv::Point2f(uniform(-options.jitter, options.jitter),
                                         uniform(-options.jitter, options.jitter));
            cv::Mat image = cube::test::draw_side(stickers, shaken, drawing);
            if (options.noise > 0.0F) {
                cv::Mat noise(image.size(), CV_8UC3);
                cv::randn(noise, cv::Scalar::all(0),
                          cv::Scalar::all(static_cast<double>(options.noise)));
                cv::add(image, noise, image);
            }
            if (options.blur > 0.0F) {
                cv::GaussianBlur(image, image, cv::Size(0, 0), static_cast<double>(options.blur));
            }
            const auto read = cube::vision::read_side(image, scene.size);
            ++tally.frames;
            if (read.has_value()) {
                ++tally.frames_read;
                for (std::size_t i = 0; i < stickers.size(); ++i) {
                    ++tally.stickers;
                    tally.stickers_right +=
                        nearest_paint(paints, read->lab.at(i)) == stickers.at(i) ? 1 : 0;
                }
                tally.found_stickers += read->stickers_found;
                tally.lattice_stickers += static_cast<long>(stickers.size());
            }
            found_here += read.has_value() ? 1 : 0;
            if (read.has_value()) {
                const cv::Point2f at = cube::vision::steady_point(*read);
                trace += fmt::format(" ({:.1f},{:.1f}|{})", at.x, at.y, read->stickers_found);
            } else {
                trace += " -";
            }
            scan.add(read, 33.0F);
            if (!options.dump.empty() && frame == options.frames - 1 && scan.reads_in_view() == 0) {
                cv::imwrite(fmt::format("{}/s{}_p{}_{}_side{}.png", options.dump, seed,
                                        static_cast<int>(scene.pitch), scene.light, shown.size()),
                            image);
                fmt::print(stderr, "  trace:{}\n", trace);
            }
        }
        found_per_side.push_back(found_here);
        counted_per_side.push_back(static_cast<int>(scan.reads_in_view()));
        tally.side_frames_read += found_here;
        tally.side_frames_counted += static_cast<long>(scan.reads_in_view());
        scan.add(std::nullopt);
    }

    ++tally.scans;
    const auto sides = scan.sides();
    if (sides.size() < 6) {
        ++tally.short_of_six;
        if (options.verbose) {
            std::string line = fmt::format("short: size {} pitch {} {} seed {}: found per side",
                                           scene.size, scene.pitch, scene.light, seed);
            for (std::size_t i = 0; i < found_per_side.size(); ++i) {
                line += fmt::format(" {}/{}", counted_per_side.at(i), found_per_side.at(i));
            }
            fmt::print(stderr, "{}\n", line);
        }
        return tally;
    }
    ++tally.scans_six;
    const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
    if (diagnosis.naming.has_value()) {
        // Each captured side is one that was shown; line it up by what was
        // drawn there (the scan holds it in the layout it was first seen in).
        for (std::size_t side = 0; side < 6 && side < diagnosis.sides.size(); ++side) {
            // diagnosis.sides are in the order of most reads; `first_seen` is the
            // group's index, which is the order the sides were shown in.
            const std::size_t drawn = diagnosis.sides.at(side).first_seen;
            if (drawn >= shown.size()) {
                continue;
            }
            const auto& truth_side = shown.at(drawn);
            for (std::size_t cell = 0; cell < truth_side.size(); ++cell) {
                const cube::model::FacetColor name =
                    diagnosis.naming->faces.at(side).at(cell).color;
                const Color want = truth_side.at(cell);
                const bool right =
                    (want == Color::White && name == cube::model::FacetColor::white) ||
                    (want == Color::Yellow && name == cube::model::FacetColor::yellow) ||
                    (want == Color::Green && name == cube::model::FacetColor::green) ||
                    (want == Color::Blue && name == cube::model::FacetColor::blue) ||
                    (want == Color::Orange && name == cube::model::FacetColor::orange) ||
                    (want == Color::Red && name == cube::model::FacetColor::red);
                ++tally.named;
                tally.named_right += right ? 1 : 0;
            }
        }
    }
    const auto made = scan.scanned_n();
    if (made.has_value()) {
        if (holds_cube(made->cube, truth)) {
            ++tally.exact;
        } else {
            ++tally.wrong;
            if (options.verbose) {
                fmt::print(stderr, "wrong: size {} pitch {} {} seed {}\n", scene.size, scene.pitch,
                           scene.light, seed);
            }
        }
    } else if (made.error() == cube::CubeError::ambiguous_scan) {
        ++tally.ambiguous;
    } else {
        ++tally.none;
        if (options.verbose) {
            fmt::print(stderr, "none: size {} pitch {} {} seed {}: error {}\n", scene.size,
                       scene.pitch, scene.light, seed, static_cast<int>(made.error()));
        }
    }
    return tally;
}

double percent(long part, long whole) {
    return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    // --image <file> <size>: what the reader makes of one picture.
    if (argc == 4 && std::string(argv[1]) == "--image") {
        const cv::Mat image = cv::imread(argv[2]);
        const auto read = cube::vision::read_side(image, std::stoi(argv[3]));
        if (read.has_value()) {
            const cv::Point2f at = cube::vision::steady_point(*read);
            fmt::print("read: found {} pitch {:.2f} centre ({:.1f},{:.1f})\n", read->stickers_found,
                       read->pitch, at.x, at.y);
        } else {
            fmt::print("no read\n");
        }
        return 0;
    }
    // --session <dir> <size> <seed>: a drawn cube written as a recorded session, for
    // cube_scan to replay (frames, manifest.json, truth.txt).
    if (argc == 5 && std::string(argv[1]) == "--session") {
        const std::filesystem::path dir = argv[2];
        const int size = std::stoi(argv[3]);
        const auto seed = static_cast<unsigned>(std::stoul(argv[4]));
        std::filesystem::create_directories(dir);
        const NCube cube =
            cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 20 * size});
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> tilt(-12.0F, 12.0F);
        std::uniform_real_distribution<float> shift(-30.0F, 30.0F);
        std::uniform_real_distribution<float> shake(-0.7F, 0.7F);
        const cube::test::Drawing drawing;
        std::string truth;
        int frame_number = 0;
        for (const Face face : cube::solver::kAllFaces) {
            std::vector<Color> stickers =
                turned(cube::nxn::outside_view(cube, face), static_cast<int>(rng() >> 30U));
            std::string word;
            for (const Color colour : stickers) {
                word += "WYGBOR"[static_cast<std::size_t>(colour)];
            }
            truth += word + "\n";
            cube::test::Pose pose;
            pose.pitch = 0.5F * 360.0F / static_cast<float>(size);
            pose.centre = {320.0F + shift(rng), 180.0F + shift(rng) / 2.0F};
            pose.degrees = tilt(rng);
            for (int i = 0; i < 14; ++i) {
                cube::test::Pose shaken = pose;
                shaken.centre += cv::Point2f(shake(rng), shake(rng));
                cv::imwrite(fmt::format("{}/rec_{:06}.jpg", dir.string(), frame_number++),
                            cube::test::draw_side(stickers, shaken, drawing));
            }
            for (int i = 0; i < 4; ++i) {
                cv::imwrite(fmt::format("{}/rec_{:06}.jpg", dir.string(), frame_number++),
                            cube::test::room(drawing.size));
            }
        }
        std::ofstream(dir / "truth.txt") << truth;
        if (size != 3) {
            std::ofstream(dir / "manifest.json") << "{ \"size\": " << size << " }";
        }
        return 0;
    }
    // --oracle <sizes> [cubes] [swaps]: how often a cube with two stickers of
    // different colours traded is still one validate() passes - the error the
    // exactly-N^2-per-colour naming makes, and the false accepts the assembly
    // cannot see - by what kind of piece the two stickers are on.
    if (argc >= 3 && std::string(argv[1]) == "--oracle") {
        const int cubes = argc > 3 ? std::stoi(argv[3]) : 100;
        const int swaps = argc > 4 ? std::stoi(argv[4]) : 400;
        const char* const names[] = {"corner", "wing", "mid-edge", "centre", "face-centre"};
        for (const auto& part : split(argv[2])) {
            const int size = std::stoi(part);
            const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(size);
            std::array<std::array<long, 5>, 5> tried{};
            std::array<std::array<long, 5>, 5> accepted{};
            std::mt19937 rng(7);
            for (int c = 0; c < cubes; ++c) {
                const NCube cube = cube::nxn::random_scramble(
                    {.size = size, .seed = static_cast<unsigned>(900 + c), .moves = 20 * size});
                for (int k = 0; k < swaps; ++k) {
                    const std::size_t a = rng() % geometry.facelet_count();
                    const std::size_t b = rng() % geometry.facelet_count();
                    std::vector<Color> stickers(cube.facelets().begin(), cube.facelets().end());
                    if (a == b || stickers[a] == stickers[b]) {
                        continue;
                    }
                    std::swap(stickers[a], stickers[b]);
                    auto ra = static_cast<std::size_t>(geometry.role(a));
                    auto rb = static_cast<std::size_t>(geometry.role(b));
                    if (ra > rb) {
                        std::swap(ra, rb);
                    }
                    ++tried[ra][rb];
                    accepted[ra][rb] += cube::nxn::validate(size, stickers).ok() ? 1 : 0;
                }
            }
            fmt::print("size {}: two stickers of different colours swapped, still a cube:\n", size);
            for (std::size_t i = 0; i < 5; ++i) {
                for (std::size_t j = i; j < 5; ++j) {
                    if (tried[i][j] > 0) {
                        fmt::print("  {:>11} x {:<11} {:>7} of {:>7} ({:.2f}%)\n", names[i],
                                   names[j], accepted[i][j], tried[i][j],
                                   percent(accepted[i][j], tried[i][j]));
                    }
                }
            }
        }
        return 0;
    }
    // --bench <size> <WxH> <pitch>: what one frame costs to read, in milliseconds.
    if (argc == 5 && std::string(argv[1]) == "--bench") {
        const int size = std::stoi(argv[2]);
        const std::string text = argv[3];
        const auto x = text.find('x');
        cube::test::Drawing drawing;
        drawing.size = {std::stoi(text.substr(0, x)), std::stoi(text.substr(x + 1))};
        cube::test::Pose pose;
        pose.pitch = std::stof(argv[4]);
        pose.centre = {static_cast<float>(drawing.size.width) / 2.0F,
                       static_cast<float>(drawing.size.height) / 2.0F};
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 3, .moves = 40});
        const cv::Mat image =
            cube::test::draw_side(cube::nxn::outside_view(cube, Face::F), pose, drawing);
        const cv::Mat nothing(drawing.size, CV_8UC3, cv::Scalar(100, 100, 100));
        for (const cv::Mat* frame : {&image, &nothing}) {
            double best = 1.0e9;
            double total = 0.0;
            const int runs = 60;
            for (int i = 0; i < runs; ++i) {
                const auto start = std::chrono::steady_clock::now();
                const auto read = cube::vision::read_side(*frame, size);
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();
                best = std::min(best, ms);
                total += ms;
                if (i == 0) {
                    fmt::print("{}: ", read.has_value() ? "side" : "empty frame");
                }
            }
            fmt::print("size {} frame {}x{}: best {:.2f} ms, mean {:.2f} ms\n", size,
                       drawing.size.width, drawing.size.height, best, total / runs);
        }
        return 0;
    }
    const Options options = parse(argc, argv);
    const unsigned threads =
        options.threads != 0 ? options.threads : std::max(1U, std::thread::hardware_concurrency());
    if (options.csv) {
        fmt::print(
            "size,light,pitch,sticker_px,extent_pct,read,sticker,named,steady,exact,ambiguous,"
            "wrong,none,short,cubes\n");
    } else {
        fmt::print(
            "{:>4} {:>8} {:>6} {:>5} {:>6} | {:>6} {:>7} {:>7} {:>7} | {:>5} {:>5} {:>5} {:>5} "
            "{:>5}\n",
            "size", "light", "pitch", "stkr", "cube%", "read%", "stkr%", "named%", "steady%",
            "exact", "ambig", "wrong", "none", "short");
    }
    const bool by_extent = !options.extents.empty();
    const std::vector<float>& values = by_extent ? options.extents : options.pitches;
    for (const int size : options.sizes) {
        for (const std::string& light : options.lights) {
            for (const float value : values) {
                Scene scene;
                scene.size = size;
                // An extent is the cube's height as a share of the frame's, in per cent.
                scene.pitch = by_extent ? value * 0.01F * static_cast<float>(options.frame.height) /
                                              static_cast<float>(size)
                                        : value;
                scene.light = light;
                scene.options = &options;
                Tally total;
                std::mutex guard;
                std::atomic<int> next = 0;
                std::vector<std::thread> pool;
                for (unsigned t = 0; t < threads; ++t) {
                    pool.emplace_back([&] {
                        for (;;) {
                            const int i = next.fetch_add(1);
                            if (i >= options.cubes) {
                                return;
                            }
                            const Tally one = run_cube(
                                scene, options.seed + static_cast<unsigned>((size * 1000) + i));
                            const std::lock_guard<std::mutex> lock(guard);
                            total.add(one);
                        }
                    });
                }
                for (std::thread& thread : pool) {
                    thread.join();
                }
                const float sticker_px = 0.8F * scene.pitch;
                const float cube_pct = 100.0F * static_cast<float>(size) * scene.pitch /
                                       static_cast<float>(options.frame.height);
                const double steady = percent(total.side_frames_counted, total.side_frames_read);
                if (options.csv) {
                    fmt::print(
                        "{},{},{:.1f},{:.1f},{:.1f},{:.2f},{:.2f},{:.2f},{:.1f},{},{},{},{},{},{}"
                        "\n",
                        size, light, scene.pitch, sticker_px, cube_pct,
                        percent(total.frames_read, total.frames),
                        percent(total.stickers_right, total.stickers),
                        percent(total.named_right, total.named), steady, total.exact,
                        total.ambiguous, total.wrong, total.none, total.short_of_six, total.scans);
                } else {
                    fmt::print(
                        "{:>4} {:>8} {:>6.1f} {:>5.1f} {:>6.0f} | {:>6.1f} {:>7.2f} {:>7.2f} "
                        "{:>7.1f} | {:>5} {:>5} {:>5} {:>5} {:>5}\n",
                        size, light, scene.pitch, sticker_px, cube_pct,
                        percent(total.frames_read, total.frames),
                        percent(total.stickers_right, total.stickers),
                        percent(total.named_right, total.named), steady, total.exact,
                        total.ambiguous, total.wrong, total.none, total.short_of_six);
                }
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
