#include "../src/input.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool ok, const char *message)
{
	if (!ok) throw std::runtime_error(message);
}

int main(int argc, char **)
{
	using sf6::standard_pad;
	using sf6::hat_direction;
	using json = nlohmann::json;
	if (argc > 1) {
		sf6::InputCapture input;
		input.start();
		std::this_thread::sleep_for(std::chrono::milliseconds(750));
		std::cout << input.snapshot().dump() << std::endl;
		return 0;
	}
	const uint16_t masks[16] = {0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200, 0, 0,
				   0x20, 0x10, 0x40, 0x80, 1, 2, 4, 8};
	for (int b = 0; b < 16; ++b) {
		auto pad = standard_pad(2, masks[b], b == 6 ? 255 : 0, b == 7 ? 255 : 0, 0, 0, 0, 0);
		check(pad["index"] == 2 && pad["mapping"] == "standard", "identity");
		for (int i = 0; i < 17; ++i)
			check(pad["buttons"][i]["pressed"] == (i == b), "button mapping");
	}
	auto pad = standard_pad(0, 0, 127, 128, -32768, 32767, 32767, -32768);
	check(pad["axes"] == json({-1.0, -1.0, 1.0, 1.0}), "axis signs and bounds");
	check(pad["buttons"][6]["pressed"] == false && pad["buttons"][7]["pressed"] == true, "trigger threshold");
	check(hat_direction(0xffffffff) == json({0, 0}) && hat_direction(0xffff) == json({0, 0}), "POV neutral");
	const int directions[8][2] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
	for (int i = 0; i < 8; ++i)
		check(hat_direction(i * 4500) == json({directions[i][0], directions[i][1]}), "POV direction");
	sf6::InputCapture input;
	for (int i = 0; i < 3; ++i) {
		input.start(); input.start();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		check(input.snapshot()["pads"].is_array(), "snapshot");
		input.stop(); input.stop();
		check(input.snapshot()["pads"].empty(), "stop clears input");
	}
	std::cout << "Controller mapping and lifecycle OK" << std::endl;
}
