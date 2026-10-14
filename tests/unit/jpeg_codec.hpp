#pragma once

// Recording writes its frames as JPEG, through OpenCV's imgcodecs. The desktop
// builds of OpenCV have the codec; the browser's does not - the web app never
// records, and a codec it does not use is code it would have to ship - so the
// tests that write and read frames say so and are skipped there, rather than
// failing on a feature that build does not have. The recorder's failure paths
// need no codec and are not skipped: a build without one is how they run there.

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgcodecs.hpp>

namespace cube::test {

// Whether this build of OpenCV has the JPEG codec (it has the encoder and the
// decoder together, or neither). A case that does not need it says what it
// expects of a build without one, rather than skipping.
//
// The writer is asked by extension. cv::haveImageReader() cannot be: it opens
// the file it is given to read its signature, so "x.jpg" is false wherever no
// such file is lying about, the desktop builds included.
inline bool have_jpeg_codec() {
    return cv::haveImageWriter(".jpg");
}

}  // namespace cube::test

// A macro because SKIP ends the test case it is called from.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CUBE_SKIP_WITHOUT_JPEG_CODEC()                      \
    do {                                                    \
        if (!::cube::test::have_jpeg_codec()) {             \
            SKIP("this build of OpenCV has no JPEG codec"); \
        }                                                   \
    } while (false)
// NOLINTEND(cppcoreguidelines-macro-usage)
