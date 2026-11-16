// Run the real tracker API in a temporary test directory, without loading OBS.
#include "../src/tracker.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	std::string root = argv[2], data = root + "/data/obs-plugins/sf6-tracker-native";
	sf6::Tracker tracker({argv[1], data + "/web", data + "/ort/sf6-onnxruntime.dll",
		data + "/models/ch_PP-OCRv4_det_infer.onnx", data + "/models/ch_PP-OCRv4_rec_infer.onnx"});
	if (!tracker.start(std::atoi(argv[3]))) { std::cerr << tracker.error() << std::endl; return 1; }
	std::cout << "READY" << std::endl;
	std::string command;
	std::getline(std::cin, command);
	tracker.stop();
}
