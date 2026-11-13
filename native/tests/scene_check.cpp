// Reads raw 640x360 BGRA pictures named on the command line and prints, for each,
// "<result_bar_likely> <battle_hud_likely>" as 0/1. Used by tests/compare_with_python.py.
#include "../src/scene.hpp"

#include <cstdio>
#include <vector>

int main(int argc, char **argv)
{
	const int w = 640, h = 360;
	std::vector<uint8_t> buf(size_t(w) * h * 4);
	for (int i = 1; i < argc; ++i) {
		FILE *fp = std::fopen(argv[i], "rb");
		if (!fp || std::fread(buf.data(), 1, buf.size(), fp) != buf.size()) {
			std::fprintf(stderr, "cannot read %s\n", argv[i]);
			return 2;
		}
		std::fclose(fp);
		sf6::Frame f{w, h, w * 4, buf.data()};
		std::printf("%d %d\n", sf6::result_bar_likely(f) ? 1 : 0, sf6::battle_hud_likely(f) ? 1 : 0);
	}
	return 0;
}
