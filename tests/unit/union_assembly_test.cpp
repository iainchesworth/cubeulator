// Union-assembly harness - hidden tag, never part of the normal suite.
//
// Companion to [.cross-session]. That harness answers "what nine colours
// did each body face actually yield in each clip"; this one answers the
// other half of the accuracy question: does the UNION of those per-face
// captures, pooled across the four clips, assemble into a physically
// valid cube?
//
// The point is the same as its companion's: every safety metric the
// tracker reports is self-consistency. Assembly validity is not. A cube
// is over-constrained - 9 facelets per colour, six distinct centres,
// permutation parity, corner/edge twist sums - so a mis-routed face has
// almost no way to slip through. is_physically_valid() is therefore an
// EXTERNAL referent, and the four clips recorded one unchanging physical
// cube, so pooling them is legitimate.
//
// Deliberately NOT a re-implementation of the validity rules: the pooled
// faces go through the real cube::solver::cube_from_scan(), which applies
// the real camera->solver permutations and the real
// Cube::is_physically_valid(). This harness only marshals data into a
// model::ScanProgress and prints what the production path returned.
//
// Input is the pooled face set, supplied as env var CUBE_UNION_FACES in
// the form
//
//   F=RYRYBGRGW,D=BRYWRRWBR,L=WBYBWRYBR,B=OBOWGRWWO
//
// - letters URFDLB for the slot, nine WYROGB facelets in the same solver
// order [.cross-session] emits, so its records paste straight in. The
// pooling RULE is the caller's choice (last-clip-wins, majority vote,
// ...); keeping it outside the binary is what lets one build measure the
// sensitivity of the verdict to that choice, which is the interesting
// question when the clips disagree.
//
// Skips (never fails) when the env var is absent, exactly like
// [.cross-session] and [.track-replay]: the corpus is recorded on demand
// from live hardware and does not ship with the repo. A partial face set
// is accepted and reported as such - cube_from_scan() will reject it with
// face_capture_incomplete, and saying so plainly is more useful than
// pretending a four-face union was a validation.
//
// std::getenv is fine here (single-threaded harness, read-only).

#include <catch2/catch_test_macros.hpp>

#include <cube/error.hpp>
#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>
#include <cube/model/scan_progress.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_conversion.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::optional<cube::model::Face> face_from_letter(char c) noexcept {
    switch (c) {
        case 'U':
            return cube::model::Face::U;
        case 'R':
            return cube::model::Face::R;
        case 'F':
            return cube::model::Face::F;
        case 'D':
            return cube::model::Face::D;
        case 'L':
            return cube::model::Face::L;
        case 'B':
            return cube::model::Face::B;
        default:
            return std::nullopt;
    }
}

std::optional<cube::model::FacetColor> color_from_letter(char c) noexcept {
    switch (c) {
        case 'W':
            return cube::model::FacetColor::white;
        case 'Y':
            return cube::model::FacetColor::yellow;
        case 'R':
            return cube::model::FacetColor::red;
        case 'O':
            return cube::model::FacetColor::orange;
        case 'G':
            return cube::model::FacetColor::green;
        case 'B':
            return cube::model::FacetColor::blue;
        default:
            return std::nullopt;  // includes '?' - an unknown cell
    }
}

constexpr std::string_view kFaceLetters = "URFDLB";

}  // namespace

TEST_CASE("pooled cross-clip faces assemble into a valid cube", "[.union-assembly]") {
    const char* spec_env = std::getenv("CUBE_UNION_FACES");
    if (spec_env == nullptr || *spec_env == '\0') {
        WARN("CUBE_UNION_FACES not set - skipping union-assembly harness");
        SUCCEED();
        return;
    }

    // Parse "F=RYRYBGRGW,D=..." into a slot -> nine-colour map.
    std::map<char, std::string> faces;
    const std::string spec(spec_env);
    for (std::size_t pos = 0; pos < spec.size();) {
        const std::size_t comma = spec.find(',', pos);
        const std::string item =
            spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = (comma == std::string::npos) ? spec.size() : comma + 1;
        if (item.size() != 11 || item[1] != '=') {
            std::cout << "union: malformed entry '" << item << "' (want F=WWWWWWWWW)\n";
            continue;
        }
        faces[item[0]] = item.substr(2);
    }

    cube::model::ScanProgress scan;
    std::map<char, int> census;
    int unknown_cells = 0;

    std::cout << "\n=== union assembly ===\n";
    for (const char letter : kFaceLetters) {
        const auto it = faces.find(letter);
        if (it == faces.end()) {
            std::cout << "  " << letter << " = <not captured in any clip>\n";
            continue;
        }
        const auto face = face_from_letter(letter);
        if (!face) {
            continue;
        }
        std::array<cube::model::Facelet, cube::model::facelets_per_face> facelets{};
        for (std::size_t i = 0; i < cube::model::facelets_per_face; ++i) {
            const char c = it->second[i];
            const auto color = color_from_letter(c);
            if (color) {
                facelets[i].color = *color;
                facelets[i].confidence = 1.0F;
                ++census[c];
            } else {
                ++unknown_cells;  // left FacetColor::unknown
            }
        }
        scan.set_face(*face, facelets);
        std::cout << "  " << letter << " = " << it->second
                  << "  centre=" << it->second[cube::model::center_facelet_index] << "\n";
    }

    std::cout << "faces_confirmed=" << scan.faces_confirmed_count() << "/6"
              << "  complete=" << (scan.is_complete() ? "yes" : "no")
              << "  unknown_cells=" << unknown_cells << "\n";

    std::cout << "colour census (a valid cube has exactly 9 of each):\n";
    for (const char c : std::string_view("WYROGB")) {
        const int n = census.count(c) != 0 ? census.at(c) : 0;
        std::cout << "  " << c << ": " << n << (n > 9 ? "   EXCEEDS 9 - impossible\n" : "\n");
    }

    const auto assembled = cube::solver::cube_from_scan(scan);
    if (assembled) {
        std::cout << "cube_from_scan: OK   is_physically_valid="
                  << (assembled->is_physically_valid() ? "TRUE" : "FALSE") << "\n";
    } else {
        std::cout << "cube_from_scan: REJECTED with CubeError="
                  << cube::to_string(assembled.error()) << "\n";
    }
    std::cout << "=== end union assembly ===\n";

    SUCCEED();
}
