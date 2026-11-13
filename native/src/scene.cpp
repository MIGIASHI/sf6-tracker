#include "scene.hpp"

#include <algorithm>
#include <cstdlib>

namespace sf6 {

namespace {

// The same cut as vision._part: fractions of the picture, at least one pixel each way.
struct Rect {
	int x0, y0, x1, y1;
};

Rect part(const Frame &f, double x0, double y0, double x1, double y1)
{
	Rect r;
	r.y0 = int(y0 * f.height);
	r.y1 = std::max(r.y0 + 1, int(y1 * f.height));
	r.x0 = int(x0 * f.width);
	r.x1 = std::max(r.x0 + 1, int(x1 * f.width));
	r.y1 = std::min(r.y1, f.height);
	r.x1 = std::min(r.x1, f.width);
	return r;
}

inline const uint8_t *pixel(const Frame &f, int x, int y)
{
	return f.data + size_t(y) * f.stride + size_t(x) * 4;
}

// Share of pixels that are white (all channels > 200) and dark (all channels < 130).
void white_dark(const Frame &f, const Rect &r, double &white, double &dark)
{
	long w = 0, d = 0, n = 0;
	for (int y = r.y0; y < r.y1; ++y) {
		for (int x = r.x0; x < r.x1; ++x) {
			const uint8_t *p = pixel(f, x, y);
			uint8_t lo = std::min({p[0], p[1], p[2]});
			uint8_t hi = std::max({p[0], p[1], p[2]});
			w += lo > 200;
			d += hi < 130;
			++n;
		}
	}
	white = n ? double(w) / n : 0.0;
	dark = n ? double(d) / n : 0.0;
}

// Hue on OpenCV's 8-bit scale (0-180), saturation and value (0-255), as cv2.COLOR_BGR2HSV gives.
void bgr_to_hsv(int b, int g, int r, int &h, int &s, int &v)
{
	int hi = std::max({b, g, r});
	int lo = std::min({b, g, r});
	int diff = hi - lo;
	v = hi;
	s = hi ? (255 * diff + hi / 2) / hi : 0;
	if (!diff) {
		h = 0;
		return;
	}
	double deg;
	if (hi == r)
		deg = 60.0 * (g - b) / diff;
	else if (hi == g)
		deg = 120.0 + 60.0 * (b - r) / diff;
	else
		deg = 240.0 + 60.0 * (r - g) / diff;
	if (deg < 0)
		deg += 360.0;
	h = int(deg / 2.0 + 0.5);
	if (h >= 180)
		h -= 180;
}

} // namespace

bool result_bar_likely(const Frame &f)
{
	if (!f.data || f.width <= 0 || f.height <= 0)
		return false;
	double wl, dl, wr, dr;
	white_dark(f, part(f, 0.430, 0.776, 0.466, 0.810), wl, dl);
	white_dark(f, part(f, 0.534, 0.776, 0.572, 0.810), wr, dr);
	return (0.05 <= wl && wl <= 0.65 && wr < 0.04 && dr > 0.45) ||
	       (0.05 <= wr && wr <= 0.65 && wl < 0.04 && dl > 0.45);
}

bool battle_hud_likely(const Frame &f)
{
	if (!f.data || f.width <= 0 || f.height <= 0)
		return false;
	Rect r = part(f, 0.475, 0.045, 0.525, 0.12);
	long hit = 0, n = 0;
	for (int y = r.y0; y < r.y1; ++y) {
		for (int x = r.x0; x < r.x1; ++x) {
			const uint8_t *p = pixel(f, x, y);
			int h, s, v;
			bgr_to_hsv(p[0], p[1], p[2], h, s, v);
			hit += h >= 130 && h <= 170 && s > 90 && v > 110;
			++n;
		}
	}
	return n && double(hit) / n >= 0.25;
}

Scene classify(const Frame &f)
{
	if (result_bar_likely(f))
		return Scene::Result;
	if (battle_hud_likely(f))
		return Scene::Battle;
	return Scene::Other;
}

double frame_difference(const Frame &a, const Frame &b, int step)
{
	if (!a.data || !b.data || a.width != b.width || a.height != b.height)
		return 255.0;
	step = std::max(step, 1);
	long sum = 0, n = 0;
	for (int y = 0; y < a.height; y += step) {
		for (int x = 0; x < a.width; x += step) {
			const uint8_t *p = pixel(a, x, y);
			const uint8_t *q = pixel(b, x, y);
			sum += std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]);
			n += 3;
		}
	}
	return n ? double(sum) / n : 0.0;
}

const char *scene_name(Scene s)
{
	switch (s) {
	case Scene::Battle:
		return "battle";
	case Scene::Result:
		return "result";
	default:
		return "other";
	}
}

} // namespace sf6
