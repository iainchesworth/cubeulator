// The picture on the scan and solve screens is the camera's frame, painted
// mirrored by QML when the camera faces the user. The mirror lives there and
// nowhere else, so what the provider serves has to be the frame exactly as it
// was delivered: left is left, and a sticker's frame coordinates are the
// pixel it sits on. If the provider flipped, the dots drawn on the picture
// (which QML flips again with it) would land on the wrong stickers.
#include <catch2/catch_test_macros.hpp>

#include "controllers/frame_image_provider.hpp"

#include <opencv2/core.hpp>

#include <QColor>
#include <QImage>
#include <QSize>

using cube::app::FrameImageProvider;

TEST_CASE("FrameImageProvider serves the camera frame unflipped", "[qt][controllers]") {
    // 2 wide, 1 high, in the camera's BGR order: blue on the left, red on the
    // right, so a flip in either direction shows.
    cv::Mat frame(1, 2, CV_8UC3);
    frame.at<cv::Vec3b>(0, 0) = cv::Vec3b(255, 0, 0);
    frame.at<cv::Vec3b>(0, 1) = cv::Vec3b(0, 0, 255);

    FrameImageProvider provider([&frame]() { return frame; });

    QSize size;
    const QImage image = provider.requestImage("latest#1", &size, QSize());

    REQUIRE(size == QSize(2, 1));
    REQUIRE(image.size() == QSize(2, 1));
    REQUIRE(image.pixelColor(0, 0) == QColor(0, 0, 255));
    REQUIRE(image.pixelColor(1, 0) == QColor(255, 0, 0));
}

TEST_CASE("FrameImageProvider serves nothing until the camera has delivered a frame",
          "[qt][controllers]") {
    FrameImageProvider provider([]() { return cv::Mat(); });

    QSize size(7, 7);
    const QImage image = provider.requestImage("latest#0", &size, QSize());

    REQUIRE(image.isNull());
    REQUIRE(size == QSize(0, 0));
}
