// ocr_check <onnxruntime lib> <det.onnx> <rec.onnx> <picture.bgr>...
// Each picture file: int32 width, int32 height, then width*height*3 bytes BGR.
// Prints one JSON line per picture: the items read from the whole picture (top 16).
#include "../src/ocr.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char **argv)
{
	if (argc < 5)
		return 2;
	sf6::Ocr ocr;
	if (!ocr.load(argv[1], argv[2], argv[3])) {
		std::fprintf(stderr, "%s\n", ocr.error().c_str());
		return 1;
	}
	for (int i = 4; i < argc; ++i) {
		std::ifstream f(argv[i], std::ios::binary);
		int32_t wh[2];
		f.read(reinterpret_cast<char *>(wh), 8);
		sf6::Image img(wh[0], wh[1]);
		f.read(reinterpret_cast<char *>(img.px.data()), std::streamsize(img.px.size()));
		auto items = ocr.read(img, nullptr, 0, 16, 1.0);
		std::printf("[");
		for (size_t k = 0; k < items.size(); ++k) {
			std::string t;
			for (char c : items[k].text) {
				if (c == '"' || c == '\\')
					t += '\\';
				t += c;
			}
			std::printf("%s{\"text\":\"%s\",\"conf\":%.3f,\"cx\":%.4f,\"cy\":%.4f}", k ? "," : "", t.c_str(),
				    items[k].conf, items[k].cx, items[k].cy);
		}
		std::printf("]\n");
	}
	return 0;
}
