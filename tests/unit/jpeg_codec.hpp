#pragma once

// Recording writes its frames as JPEG, through OpenCV's imgcodecs. The desktop
// builds of OpenCV have the codec; the browser's does not - the web app never
// records, and a codec it does not use is code it would have to ship - so the
// tests that write and read frames say so and are skipped there, rather than
// failing on a feature that build does not have.

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgcodecs.hpp>

// A macro because SKIP ends the test case it is called from.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CUBE_SKIP_WITHOUT_JPEG_CODEC()                                       \
    do {                                                                     \
        if (!cv::haveImageWriter(".jpg") || !cv::haveImageReader("x.jpg")) { \
            SKIP("this build of OpenCV has no JPEG codec");                  \
        }                                                                    \
    } while (false)
// NOLINTEND(cppcoreguidelines-macro-usage)
