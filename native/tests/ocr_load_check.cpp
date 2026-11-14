// Load both OCR models with OBS's restricted DLL search policy and relative data paths.
#include "../src/ocr.hpp"
#include <cstdio>
#include <windows.h>

int main(int argc, char **argv)
{
	if (argc != 4)
		return 2;
	if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
		return 3;
	sf6::Ocr ocr;
	if (!ocr.load(argv[1], argv[2], argv[3]) || !ocr.ready()) {
		std::fprintf(stderr, "OCR initialization failed: %s\n", ocr.error().c_str());
		return 1;
	}
	std::puts("PASS: OCR runtime and both models loaded with relative paths and restricted DLL search");
}
