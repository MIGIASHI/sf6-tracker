// A small BGR picture type and the few operations the reading needs (crop, resize, borders).
// The resizes follow OpenCV's pixel-centre conventions so the text reading sees what the Python
// tracker sees.
#pragma once

#include <cstdint>
#include <vector>

#include "scene.hpp"

namespace sf6 {

struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> px; // B, G, R per pixel, rows top to bottom

	Image() = default;
	Image(int width, int height) : w(width), h(height), px(size_t(width) * height * 3, 0) {}
	bool empty() const { return w <= 0 || h <= 0; }
	uint8_t *at(int x, int y) { return px.data() + (size_t(y) * w + x) * 3; }
	const uint8_t *at(int x, int y) const { return px.data() + (size_t(y) * w + x) * 3; }
};

// x, y, w, h as fractions of the picture.
struct Roi {
	double x = 0, y = 0, w = 1, h = 1;
};

Image from_frame(const Frame &f);

// Same rounding as vision.crop: at least 2 pixels each way, inside the picture.
Image crop(const Image &img, const Roi &roi);
// Pixel rectangle [x0, x1) x [y0, y1), clipped to the picture.
Image crop_px(const Image &img, int x0, int y0, int x1, int y1);

Image resize_linear(const Image &img, int w, int h); // cv2.INTER_LINEAR
Image resize_area(const Image &img, int w, int h);   // cv2.INTER_AREA (for shrinking)
Image resize_cubic(const Image &img, int w, int h);  // cv2.INTER_CUBIC (for enlarging)

Image border_replicate(const Image &img, int pad);
Image border_constant(const Image &img, int top, int bottom, int left, int right);

} // namespace sf6
