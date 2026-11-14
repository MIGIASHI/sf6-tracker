// replay <onnxruntime lib> <models dir> <data dir> <frames file>
// Frames file: repeated [double time][int32 w][int32 h][w*h*3 BGR]. Runs the native Detector on
// them with the settings in <data dir>/config.json and leaves the history in <data dir>/history.json.
#include "../src/detector.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char **argv)
{
	if (argc < 5)
		return 2;
	std::string models = argv[2];
	sf6::Ocr ocr;
	if (!ocr.load(argv[1], models + "/ch_PP-OCRv4_det_infer.onnx", models + "/ch_PP-OCRv4_rec_infer.onnx")) {
		std::fprintf(stderr, "%s\n", ocr.error().c_str());
		return 1;
	}
	sf6::Config cfg(argv[3]);
	sf6::Store store(argv[3]);
	sf6::Detector det(cfg, store, ocr);
	std::ifstream f(argv[4], std::ios::binary);
	for (;;) {
		double t;
		int32_t wh[2];
		if (!f.read(reinterpret_cast<char *>(&t), 8) || !f.read(reinterpret_cast<char *>(wh), 8))
			break;
		sf6::Image img(wh[0], wh[1]);
		f.read(reinterpret_cast<char *>(img.px.data()), std::streamsize(img.px.size()));
		std::vector<uint8_t> bgra(size_t(img.w) * img.h * 4, 255);
		for (size_t i = 0, n = size_t(img.w) * img.h; i < n; ++i)
			for (int c = 0; c < 3; ++c)
				bgra[i * 4 + c] = img.px[i * 3 + c];
		sf6::Frame look{img.w, img.h, img.w * 4, bgra.data()};
		det.process(look, img, t);
		std::printf("%.1f %s\n", t, det.debug().dump().c_str());
	}
	return 0;
}
