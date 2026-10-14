#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

namespace cube::vision {

// A camera frame made ready to be read.
//
// Every reader begins the same way: bring the frame down to the width the
// readers are tuned for, take its brightest channel, threshold it against its
// surroundings and trace the outlines. That is most of what a read costs
// (resize, blur, threshold and contours are 1.7 ms of read_side's 2.0 at the
// working width), and a frame in which no side is square to the camera is
// handed to a second reader, which used to do all of it again. Made once per
// frame, it is shared by read_side (direct_scan.hpp) and read_corner
// (corner_view.hpp).
struct PreparedFrame {
    // The frame at the working width, or the frame itself when it is already
    // no wider.
    cv::Mat working;
    // Input pixels per working pixel: what turns a position found in `working`
    // back into the frame's own.
    float scale = 1.0F;
    // The brightest channel of `working` (so a saturated blue or red sticker
    // still stands clear of black plastic), lightly blurred: the picture the
    // outlines are taken from, and the one to ask how bright a place is.
    cv::Mat brightness;
    // Outlines of the bright regions of `working` against their surroundings:
    // the stickers, and anything else bright and bounded by something darker.
    std::vector<std::vector<cv::Point>> contours;
};

// Nothing for a frame that is empty or is not 8-bit BGR.
[[nodiscard]] std::optional<PreparedFrame> prepare_frame(const cv::Mat& bgr);

// The outlines of a prepared frame taken against a narrower block than
// PreparedFrame::contours, for a reader that found nothing in those.
//
// prepare_frame judges each pixel against the mean of a block 0.12 of the
// frame's height across, about one spacing between stickers. A dark sticker
// beside a bright one is lost to that: the block takes in the bright
// neighbour, the mean climbs towards it, and the sticker passes only where the
// block has not yet reached it. Clipped on that side, its outline is small and
// off-centre, and a row of them next to white stickers or a bright room is not
// an outline at all. A warm lamp makes a real cube's green and blue like that
// (L 16.8 and 11 beside a white at 65). A block 0.06 across stays inside the
// sticker's own surroundings, plastic and all.
//
// It is not what every frame is taken with: it is the noisier of the two.
// Taken that way throughout, 400 recorded daylight frames read 222 where the
// first reads 269, though a session under a warm lamp read 647 where it reads
// 583. As the second look at a frame the first gave nothing, no frame the
// first read is different. It costs the threshold and the contours again, from
// the same brightness.
[[nodiscard]] std::vector<std::vector<cv::Point>> narrow_contours(const cv::Mat& brightness);

}  // namespace cube::vision
