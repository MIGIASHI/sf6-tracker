#include "ocr.hpp"

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sf6 {

namespace {

const OrtApi *load_ort(const std::string &path, std::string &err)
{
	using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
	GetApiBase get = nullptr;
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	std::wstring wpath(size_t(n > 0 ? n : 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), n);
	HMODULE lib = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!lib) {
		err = "cannot load " + path + " (error " + std::to_string(GetLastError()) + ")";
		return nullptr;
	}
	get = reinterpret_cast<GetApiBase>(GetProcAddress(lib, "OrtGetApiBase"));
#else
	void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!lib) {
		err = std::string("cannot load ") + path + ": " + dlerror();
		return nullptr;
	}
	get = reinterpret_cast<GetApiBase>(dlsym(lib, "OrtGetApiBase"));
#endif
	if (!get) {
		err = "OrtGetApiBase missing in " + path;
		return nullptr;
	}
	const OrtApi *api = get()->GetApi(ORT_API_VERSION);
	if (!api)
		err = "this ONNX Runtime is too old (needs API " + std::to_string(ORT_API_VERSION) + ")";
	return api;
}

bool read_file(const std::string &path, std::vector<char> &out)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	std::wstring wpath(size_t(n > 0 ? n : 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), n);
	std::ifstream f(wpath.c_str(), std::ios::binary);
#else
	std::ifstream f(path, std::ios::binary);
#endif
	if (!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return !out.empty();
}

// RapidOCR's preprocess: sides that are too long or too short are scaled to multiples of 32.
Image fit_sides(const Image &img, double &ratio_w, double &ratio_h)
{
	ratio_w = ratio_h = 1.0;
	Image cur = img;
	auto scale = [&](double ratio) {
		int rh = int(std::round(int(cur.h * ratio) / 32.0) * 32);
		int rw = int(std::round(int(cur.w * ratio) / 32.0) * 32);
		if (rw <= 0 || rh <= 0)
			return;
		ratio_h = double(cur.h) / rh;
		ratio_w = double(cur.w) / rw;
		cur = resize_linear(cur, rw, rh);
	};
	if (std::max(cur.w, cur.h) > 2000)
		scale(2000.0 / std::max(cur.w, cur.h));
	if (std::min(cur.w, cur.h) < 30)
		scale(30.0 / std::min(cur.w, cur.h));
	return cur;
}

std::vector<float> to_chw(const Image &img, int width)
{
	// (x / 255 - 0.5) / 0.5, channel order kept (BGR), padded with zeros to `width`
	std::vector<float> out(size_t(3) * img.h * width, 0.f);
	for (int y = 0; y < img.h; ++y)
		for (int x = 0; x < img.w; ++x) {
			const uint8_t *p = img.at(x, y);
			for (int c = 0; c < 3; ++c)
				out[(size_t(c) * img.h + y) * width + x] = p[c] / 127.5f - 1.f;
		}
	return out;
}

} // namespace

struct Ocr::Impl {
	Ort::Env env{nullptr};
	Ort::Session det{nullptr}, rec{nullptr};
	std::string det_in, det_out, rec_in, rec_out;
	std::vector<std::string> chars; // index 0 = blank, last = space
};

Ocr::Ocr() = default;
Ocr::~Ocr() = default;

bool Ocr::ready() const
{
	return impl_ != nullptr;
}

bool Ocr::load(const std::string &ort_library, const std::string &det_model, const std::string &rec_model)
{
	std::lock_guard<std::mutex> lock(mutex_);
	impl_.reset();
	const OrtApi *api = load_ort(ort_library, error_);
	if (!api)
		return false;
	Ort::InitApi(api);
	try {
		auto impl = std::make_unique<Impl>();
		impl->env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "sf6-tracker");
		impl->env.DisableTelemetryEvents(); // nothing leaves this PC
		Ort::SessionOptions so;
		// One quiet thread: the game and OBS always come first.
		so.SetIntraOpNumThreads(1);
		so.SetInterOpNumThreads(1);
		so.SetExecutionMode(ORT_SEQUENTIAL);
		so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		so.AddConfigEntry("session.intra_op.allow_spinning", "0");
		so.AddConfigEntry("session.inter_op.allow_spinning", "0");

		std::vector<char> det_bytes, rec_bytes;
		if (!read_file(det_model, det_bytes) || !read_file(rec_model, rec_bytes)) {
			error_ = "cannot read the OCR models";
			return false;
		}
		impl->det = Ort::Session(impl->env, det_bytes.data(), det_bytes.size(), so);
		impl->rec = Ort::Session(impl->env, rec_bytes.data(), rec_bytes.size(), so);
		Ort::AllocatorWithDefaultOptions alloc;
		impl->det_in = impl->det.GetInputNameAllocated(0, alloc).get();
		impl->det_out = impl->det.GetOutputNameAllocated(0, alloc).get();
		impl->rec_in = impl->rec.GetInputNameAllocated(0, alloc).get();
		impl->rec_out = impl->rec.GetOutputNameAllocated(0, alloc).get();

		Ort::ModelMetadata meta = impl->rec.GetModelMetadata();
		auto text = meta.LookupCustomMetadataMapAllocated("character", alloc);
		if (!text) {
			error_ = "the recognition model has no character list";
			return false;
		}
		impl->chars.push_back("");
		std::string all = text.get();
		size_t start = 0;
		while (start <= all.size()) {
			size_t end = all.find('\n', start);
			if (end == std::string::npos)
				end = all.size();
			std::string line = all.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (!(end == all.size() && line.empty()))
				impl->chars.push_back(line);
			start = end + 1;
		}
		impl->chars.push_back(" ");
		impl_ = std::move(impl);
		error_.clear();
		return true;
	} catch (const std::exception &e) {
		error_ = std::string("OCR: ") + e.what();
		return false;
	}
}

std::vector<Ocr::Box> Ocr::detect(const Image &input)
{
	std::vector<Box> boxes;
	if (!impl_ || input.empty())
		return boxes;

	double ratio_w, ratio_h;
	Image img = fit_sides(input, ratio_w, ratio_h);

	// Letterbox very flat or very low pictures (RapidOCR: min_height 30, width/height 8).
	int pad_top = 0;
	if (img.h <= 30 || double(img.w) / img.h > 8.0) {
		int new_h = std::max(int(img.w / 8.0), 30) * 2;
		pad_top = std::abs(new_h - img.h) / 2;
		img = border_constant(img, pad_top, pad_top, 0, 0);
	}

	// Sides to multiples of 32 (the longest side is never over the limit at the sizes we use).
	int limit = std::max(img.w, img.h) < 960 ? 960 : (std::max(img.w, img.h) < 1500 ? 1500 : 2000);
	double ratio = std::max(img.w, img.h) > limit ? double(limit) / std::max(img.w, img.h) : 1.0;
	int rh = int(std::round(int(img.h * ratio) / 32.0) * 32);
	int rw = int(std::round(int(img.w * ratio) / 32.0) * 32);
	if (rw <= 0 || rh <= 0)
		return boxes;
	Image net = resize_linear(img, rw, rh);
	std::vector<float> data = to_chw(net, rw);

	std::vector<int64_t> shape{1, 3, rh, rw};
	Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	Ort::Value in = Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), shape.data(), shape.size());
	const char *in_names[] = {impl_->det_in.c_str()};
	const char *out_names[] = {impl_->det_out.c_str()};
	auto out = impl_->det.Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
	auto oshape = out[0].GetTensorTypeAndShapeInfo().GetShape();
	const int H = int(oshape[2]), W = int(oshape[3]);
	const float *pred = out[0].GetTensorData<float>();

	// Binarize at 0.3, dilate with a 2x2 kernel (anchor at its centre, as cv2.dilate does).
	std::vector<uint8_t> seg(size_t(W) * H), mask(size_t(W) * H);
	for (size_t i = 0; i < seg.size(); ++i)
		seg[i] = pred[i] > 0.3f;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			uint8_t v = seg[size_t(y) * W + x];
			if (x > 0)
				v |= seg[size_t(y) * W + x - 1];
			if (y > 0) {
				v |= seg[size_t(y - 1) * W + x];
				if (x > 0)
					v |= seg[size_t(y - 1) * W + x - 1];
			}
			mask[size_t(y) * W + x] = v;
		}

	// Each 8-connected blob is one text candidate. Text in SF6 is level, so its smallest
	// enclosing rectangle is its bounding box.
	const int dest_w = img.w, dest_h = img.h;
	std::vector<int> stack;
	int found = 0;
	for (int sy = 0; sy < H && found < 1000; ++sy)
		for (int sx = 0; sx < W && found < 1000; ++sx) {
			if (mask[size_t(sy) * W + sx] != 1)
				continue;
			int x0 = sx, x1 = sx, y0 = sy, y1 = sy;
			stack.assign(1, sy * W + sx);
			mask[size_t(sy) * W + sx] = 2;
			while (!stack.empty()) {
				int p = stack.back();
				stack.pop_back();
				int px = p % W, py = p / W;
				x0 = std::min(x0, px);
				x1 = std::max(x1, px);
				y0 = std::min(y0, py);
				y1 = std::max(y1, py);
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = px + dx, ny = py + dy;
						if (nx < 0 || ny < 0 || nx >= W || ny >= H)
							continue;
						uint8_t &m = mask[size_t(ny) * W + nx];
						if (m == 1) {
							m = 2;
							stack.push_back(ny * W + nx);
						}
					}
			}
			++found;
			double bw = x1 - x0, bh = y1 - y0;
			if (std::min(bw, bh) < 3)
				continue;
			double sum = 0;
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x)
					sum += pred[size_t(y) * W + x];
			double score = sum / ((x1 - x0 + 1.0) * (y1 - y0 + 1.0));
			if (score < 0.5)
				continue;
			// Grow by area * 1.6 / perimeter on every side (pyclipper's round offset of a rectangle).
			double d = bw * bh * 1.6 / (2 * (bw + bh));
			double ex0 = x0 - d, ex1 = x1 + d, ey0 = y0 - d, ey1 = y1 + d;
			if (std::min(ex1 - ex0, ey1 - ey0) < 5)
				continue;
			auto sxp = [&](double v) { return std::clamp(std::round(v / W * dest_w), 0.0, double(dest_w)); };
			auto syp = [&](double v) { return std::clamp(std::round(v / H * dest_h), 0.0, double(dest_h)); };
			double bx0 = std::min(sxp(ex0), dest_w - 1.0), bx1 = std::min(sxp(ex1), dest_w - 1.0);
			double by0 = std::min(syp(ey0), dest_h - 1.0), by1 = std::min(syp(ey1), dest_h - 1.0);
			if (int(bx1 - bx0) <= 3 || int(by1 - by0) <= 3)
				continue;
			// Back to the picture we were given: remove the letterbox, undo the side fitting.
			boxes.push_back({bx0 * ratio_w, (by0 - pad_top) * ratio_h, bx1 * ratio_w, (by1 - pad_top) * ratio_h});
		}
	return boxes;
}

std::pair<std::string, double> Ocr::recognize(const Image &line)
{
	if (!impl_ || line.empty())
		return {"", 0.0};
	double rw_, rh_;
	Image img = fit_sides(line, rw_, rh_);

	const int H = 48;
	double ratio = double(img.w) / img.h;
	int width = int(H * std::max(320.0 / 48.0, ratio));
	int resized_w = std::min(width, int(std::ceil(H * ratio)));
	Image net = resize_linear(img, resized_w, H);
	std::vector<float> data = to_chw(net, width);

	std::vector<int64_t> shape{1, 3, H, width};
	Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	Ort::Value in = Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), shape.data(), shape.size());
	const char *in_names[] = {impl_->rec_in.c_str()};
	const char *out_names[] = {impl_->rec_out.c_str()};
	auto out = impl_->rec.Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
	auto oshape = out[0].GetTensorTypeAndShapeInfo().GetShape();
	const int T = int(oshape[1]), C = int(oshape[2]);
	const float *p = out[0].GetTensorData<float>();

	// Greedy CTC: best class per step, drop repeats and blanks.
	std::string text;
	double conf = 0;
	int kept = 0, last = -1;
	for (int t = 0; t < T; ++t) {
		const float *row = p + size_t(t) * C;
		int best = int(std::max_element(row, row + C) - row);
		if (best != last && best != 0 && best < int(impl_->chars.size())) {
			text += impl_->chars[size_t(best)];
			conf += row[best];
			++kept;
		}
		last = best;
	}
	return {text, kept ? conf / kept : 0.0};
}

Items Ocr::read(const Image &frame, const Roi *roi, int det_width, int top, double scale)
{
	std::lock_guard<std::mutex> lock(mutex_);
	Items items;
	if (!impl_ || frame.empty())
		return items;
	try {
		Image img = roi ? crop(frame, *roi) : frame;
		double k = scale > 0 ? scale : (img.h < 240 ? std::min(4.0, 240.0 / img.h) : 1.0);
		if (k != 1.0)
			img = resize_cubic(img, int(std::lround(img.w * k)), int(std::lround(img.h * k)));

		// Find text on a small copy (with a 16 px border), read it from the big one.
		double ks = det_width > 0 ? std::min(1.0, double(det_width) / img.w) : 1.0;
		Image small = ks < 1.0 ? resize_area(img, int(std::lround(img.w * ks)), int(std::lround(img.h * ks))) : img;
		const int pad = 16;
		std::vector<Box> found;
		for (const Box &b : detect(border_replicate(small, pad))) {
			double x0 = std::max(0.0, (b.x0 - pad) / ks), y0 = std::max(0.0, (b.y0 - pad) / ks);
			double x1 = std::min(double(img.w), (b.x1 - pad) / ks), y1 = std::min(double(img.h), (b.y1 - pad) / ks);
			if (x1 - x0 >= 4 && y1 - y0 >= 4)
				found.push_back({x0, y0, x1, y1});
		}
		std::stable_sort(found.begin(), found.end(),
				 [](const Box &a, const Box &b) { return (a.y1 - a.y0) > (b.y1 - b.y0); });
		if (top > 0 && int(found.size()) > top)
			found.resize(size_t(top));

		const Roi r = roi ? *roi : Roi{};
		for (const Box &b : found) {
			int m = int((b.y1 - b.y0) * 0.12) + 2;
			Image piece = crop_px(img, std::max(0, int(b.x0) - m), std::max(0, int(b.y0) - m), int(b.x1) + m, int(b.y1) + m);
			auto res = recognize(piece);
			if (res.first.empty() || res.second < 0.5)
				continue;
			TextItem it;
			it.text = res.first;
			it.conf = std::round(res.second * 1000) / 1000;
			it.x = r.x + b.x0 / img.w * r.w;
			it.y = r.y + b.y0 / img.h * r.h;
			it.w = (b.x1 - b.x0) / img.w * r.w;
			it.h = (b.y1 - b.y0) / img.h * r.h;
			it.cx = it.x + it.w / 2;
			it.cy = it.y + it.h / 2;
			items.push_back(std::move(it));
		}
	} catch (const std::exception &e) {
		error_ = std::string("OCR: ") + e.what();
		items.clear();
	}
	return items;
}

} // namespace sf6
