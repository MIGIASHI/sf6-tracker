#include "image.hpp"

#include <algorithm>
#include <cmath>

namespace sf6 {

Image from_frame(const Frame &f)
{
	Image out(f.width, f.height);
	for (int y = 0; y < f.height; ++y) {
		const uint8_t *s = f.data + size_t(y) * f.stride;
		uint8_t *d = out.at(0, y);
		for (int x = 0; x < f.width; ++x, s += 4, d += 3) {
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
		}
	}
	return out;
}

Image crop_px(const Image &img, int x0, int y0, int x1, int y1)
{
	x0 = std::clamp(x0, 0, img.w);
	x1 = std::clamp(x1, x0, img.w);
	y0 = std::clamp(y0, 0, img.h);
	y1 = std::clamp(y1, y0, img.h);
	Image out(x1 - x0, y1 - y0);
	for (int y = y0; y < y1; ++y)
		std::copy(img.at(x0, y), img.at(x0, y) + size_t(x1 - x0) * 3, out.at(0, y - y0));
	return out;
}

Image crop(const Image &img, const Roi &r)
{
	const int w = img.w, h = img.h;
	auto rnd = [](double v) { return int(std::nearbyint(v)); }; // Python round(): half to even
	int x0 = std::max(0, std::min(w - 2, rnd(r.x * w)));
	int y0 = std::max(0, std::min(h - 2, rnd(r.y * h)));
	int x1 = std::max(x0 + 2, std::min(w, rnd((r.x + r.w) * w)));
	int y1 = std::max(y0 + 2, std::min(h, rnd((r.y + r.h) * h)));
	return crop_px(img, x0, y0, x1, y1);
}

Image resize_linear(const Image &img, int w, int h)
{
	Image out(w, h);
	if (img.empty() || w <= 0 || h <= 0)
		return out;
	const double sx = double(img.w) / w, sy = double(img.h) / h;
	std::vector<int> x0s(w), x1s(w);
	std::vector<float> fxs(w);
	for (int x = 0; x < w; ++x) {
		double fx = (x + 0.5) * sx - 0.5;
		int ix = int(std::floor(fx));
		float a = float(fx - ix);
		if (ix < 0) {
			ix = 0;
			a = 0;
		}
		if (ix >= img.w - 1) {
			ix = img.w - 1;
			a = 0;
		}
		x0s[x] = ix;
		x1s[x] = std::min(ix + 1, img.w - 1);
		fxs[x] = a;
	}
	for (int y = 0; y < h; ++y) {
		double fy = (y + 0.5) * sy - 0.5;
		int iy = int(std::floor(fy));
		float b = float(fy - iy);
		if (iy < 0) {
			iy = 0;
			b = 0;
		}
		if (iy >= img.h - 1) {
			iy = img.h - 1;
			b = 0;
		}
		int iy1 = std::min(iy + 1, img.h - 1);
		uint8_t *d = out.at(0, y);
		for (int x = 0; x < w; ++x, d += 3) {
			const uint8_t *p00 = img.at(x0s[x], iy), *p01 = img.at(x1s[x], iy);
			const uint8_t *p10 = img.at(x0s[x], iy1), *p11 = img.at(x1s[x], iy1);
			float a = fxs[x];
			for (int c = 0; c < 3; ++c) {
				float top = p00[c] + (p01[c] - p00[c]) * a;
				float bot = p10[c] + (p11[c] - p10[c]) * a;
				d[c] = uint8_t(std::clamp(int(std::lround(top + (bot - top) * b)), 0, 255));
			}
		}
	}
	return out;
}

Image resize_cubic(const Image &img, int w, int h)
{
	Image out(w, h);
	if (img.empty() || w <= 0 || h <= 0)
		return out;
	const double sx = double(img.w) / w, sy = double(img.h) / h;
	auto weights = [](float t, float k[4]) {
		const float A = -0.75f; // OpenCV's cubic
		k[0] = ((A * (t + 1) - 5 * A) * (t + 1) + 8 * A) * (t + 1) - 4 * A;
		k[1] = ((A + 2) * t - (A + 3)) * t * t + 1;
		k[2] = ((A + 2) * (1 - t) - (A + 3)) * (1 - t) * (1 - t) + 1;
		k[3] = 1.f - k[0] - k[1] - k[2];
	};
	std::vector<int> xi(w);
	std::vector<float> xk(size_t(w) * 4);
	for (int x = 0; x < w; ++x) {
		double fx = (x + 0.5) * sx - 0.5;
		xi[x] = int(std::floor(fx));
		weights(float(fx - xi[x]), &xk[size_t(x) * 4]);
	}
	std::vector<float> row(size_t(img.w) * 3);
	for (int y = 0; y < h; ++y) {
		double fy = (y + 0.5) * sy - 0.5;
		int iy = int(std::floor(fy));
		float ky[4];
		weights(float(fy - iy), ky);
		std::fill(row.begin(), row.end(), 0.f);
		for (int j = 0; j < 4; ++j) {
			int yy = std::clamp(iy - 1 + j, 0, img.h - 1);
			const uint8_t *s = img.at(0, yy);
			for (size_t i = 0; i < row.size(); ++i)
				row[i] += s[i] * ky[j];
		}
		uint8_t *d = out.at(0, y);
		for (int x = 0; x < w; ++x, d += 3) {
			const float *k = &xk[size_t(x) * 4];
			for (int c = 0; c < 3; ++c) {
				float v = 0;
				for (int i = 0; i < 4; ++i)
					v += row[size_t(std::clamp(xi[x] - 1 + i, 0, img.w - 1)) * 3 + c] * k[i];
				d[c] = uint8_t(std::clamp(int(std::lround(v)), 0, 255));
			}
		}
	}
	return out;
}

Image resize_area(const Image &img, int w, int h)
{
	if (img.empty() || w <= 0 || h <= 0)
		return Image(std::max(w, 0), std::max(h, 0));
	if (w >= img.w && h >= img.h)
		return resize_linear(img, w, h);
	Image out(w, h);
	const double sx = double(img.w) / w, sy = double(img.h) / h;
	for (int y = 0; y < h; ++y) {
		double y0 = y * sy, y1 = (y + 1) * sy;
		for (int x = 0; x < w; ++x) {
			double x0 = x * sx, x1 = (x + 1) * sx;
			double acc[3] = {0, 0, 0}, area = 0;
			for (int yy = int(y0); yy < std::min(img.h, int(std::ceil(y1))); ++yy) {
				double wy = std::min(y1, yy + 1.0) - std::max(y0, double(yy));
				if (wy <= 0)
					continue;
				for (int xx = int(x0); xx < std::min(img.w, int(std::ceil(x1))); ++xx) {
					double wx = std::min(x1, xx + 1.0) - std::max(x0, double(xx));
					if (wx <= 0)
						continue;
					const uint8_t *p = img.at(xx, yy);
					for (int c = 0; c < 3; ++c)
						acc[c] += p[c] * wx * wy;
					area += wx * wy;
				}
			}
			uint8_t *d = out.at(x, y);
			for (int c = 0; c < 3; ++c)
				d[c] = uint8_t(std::clamp(int(std::lround(acc[c] / area)), 0, 255));
		}
	}
	return out;
}

Image border_replicate(const Image &img, int pad)
{
	Image out(img.w + 2 * pad, img.h + 2 * pad);
	for (int y = 0; y < out.h; ++y) {
		int sy = std::clamp(y - pad, 0, img.h - 1);
		for (int x = 0; x < out.w; ++x) {
			int sx = std::clamp(x - pad, 0, img.w - 1);
			const uint8_t *s = img.at(sx, sy);
			uint8_t *d = out.at(x, y);
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
		}
	}
	return out;
}

Image border_constant(const Image &img, int top, int bottom, int left, int right)
{
	Image out(img.w + left + right, img.h + top + bottom);
	for (int y = 0; y < img.h; ++y)
		std::copy(img.at(0, y), img.at(0, y) + size_t(img.w) * 3, out.at(left, y + top));
	return out;
}

} // namespace sf6
