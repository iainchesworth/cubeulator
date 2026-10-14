#pragma once

#include <opencv2/core.hpp>

#include <optional>

namespace cube::vision {

// The colour of one sticker, as float CIELAB (L in 0..100), read from a
// square patch of `bgr` centred on `centre` and `2 * half_width + 1` pixels
// across. A patch that runs off the frame is clipped to it; nothing is
// returned when fewer than nine pixels are left.
//
// This is the colour of the sticker's plastic, which is not always the colour
// of the middle value of the patch. A printed logo on a centre sticker (a
// Rubik's cube's white centre carries a wordmark in four colours across its
// middle, which is where a patch is laid), the dark gap at a sticker's edge, a
// highlight and a fingertip are all pixels of some other colour, and a
// per-channel median is carried towards them as soon as they are a sizeable
// share of the patch, and fails outright once they are half of it.
//
// So the colour is the densest cluster of the patch's pixels: the colour the
// largest part of it shares, then the median of what lies near it. The
// plastic is that cluster for as long as it is the largest single colour in
// the patch, which a logo of several colours does not stop it being even when
// it covers more than half. When no cluster holds a quarter of the patch (a
// gradient, a scatter) the plain per-channel median is returned.
//
// Nothing in the colour names it, and nothing here knows which sticker it is:
// a finger across most of the patch reads as the finger.
[[nodiscard]] std::optional<cv::Vec3f> sticker_lab(const cv::Mat& bgr, const cv::Point2f& centre,
                                                   int half_width);

}  // namespace cube::vision
