// Text reading with the same PP-OCRv4 models and the same steps as the Python tracker
// (RapidOCR 1.4.4 as driven by sf6tracker/vision.py Ocr._staged): find text boxes on the
// picture, keep the tallest few, read each from the full-size picture.
//
// ONNX Runtime is loaded at run time from a path we choose (LoadLibrary / dlopen), so it never
// clashes with another copy of onnxruntime.dll that some other OBS plugin may have loaded.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "image.hpp"

namespace sf6 {

struct TextItem {
	std::string text; // UTF-8
	double conf = 0;
	double x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0; // fractions of the full picture
};

using Items = std::vector<TextItem>;

class Ocr {
public:
	Ocr();
	~Ocr();

	// ort_library: path to the ONNX Runtime shared library. Returns false and fills error on failure.
	bool load(const std::string &ort_library, const std::string &det_model, const std::string &rec_model);
	bool ready() const;
	const std::string &error() const { return error_; }

	// Same meaning as vision.Ocr.read: roi null = whole picture; det_width > 0 finds text on a copy
	// shrunk to that width; top > 0 keeps only the N tallest pieces; scale > 0 enlarges the crop.
	Items read(const Image &frame, const Roi *roi, int det_width = 0, int top = 0, double scale = 0);

	// The steps on their own, for tests: boxes found on a picture (x0, y0, x1, y1 in its pixels),
	// and the text of one line picture.
	struct Box {
		double x0, y0, x1, y1;
	};
	std::vector<Box> detect(const Image &img);
	std::pair<std::string, double> recognize(const Image &line);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	std::string error_;
	std::mutex mutex_;
};

} // namespace sf6
