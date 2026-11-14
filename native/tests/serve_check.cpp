// serve_check <onnxruntime lib> <models dir> <web dir> <data dir> <port> <frames file>
// Runs the whole native tracker (worker + web server) outside OBS, handing it the frames from
// the file whenever it asks for one, then keeps serving for 60 s so the pages can be checked.
#include "../src/tracker.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

int main(int argc, char **argv)
{
	if (argc < 7)
		return 2;
	std::string models = argv[2];
	sf6::TrackerPaths p{argv[4], argv[3], argv[1], models + "/ch_PP-OCRv4_det_infer.onnx",
			    models + "/ch_PP-OCRv4_rec_infer.onnx"};
	sf6::Tracker t(p);
	if (!t.start(std::atoi(argv[5]))) {
		std::fprintf(stderr, "start failed: %s\n", t.error().c_str());
		return 1;
	}
	std::ifstream f(argv[6], std::ios::binary);
	int fed = 0;
	auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
	while (std::chrono::steady_clock::now() < until) {
		if (t.wants_frame() && f) {
			double ts;
			int32_t wh[2];
			if (f.read(reinterpret_cast<char *>(&ts), 8) && f.read(reinterpret_cast<char *>(wh), 8)) {
				std::vector<uint8_t> bgr(size_t(wh[0]) * wh[1] * 3), bgra(size_t(wh[0]) * wh[1] * 4, 255);
				f.read(reinterpret_cast<char *>(bgr.data()), std::streamsize(bgr.size()));
				for (size_t i = 0; i < size_t(wh[0]) * wh[1]; ++i)
					for (int c = 0; c < 3; ++c)
						bgra[i * 4 + c] = bgr[i * 3 + c];
				t.put_frame(bgra.data(), wh[0], wh[1], wh[0] * 4);
				std::printf("fed %d\n", ++fed);
				std::fflush(stdout);
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	t.stop();
	return 0;
}
