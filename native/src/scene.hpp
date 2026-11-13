// Cheap looks at an SF6 picture, without any text reading.
//
// These are C++ ports of the colour checks in sf6tracker/vision.py (result_bar_likely and
// battle_hud_likely), with the same places on screen and the same thresholds, so the native
// plugin and the Python tracker decide the same way on the same picture.
#pragma once

#include <cstdint>

namespace sf6 {

// A picture in memory, 4 bytes per pixel in B, G, R, A order (what OBS gives for GS_BGRA).
struct Frame {
	int width = 0;
	int height = 0;
	int stride = 0; // bytes from one row to the next
	const uint8_t *data = nullptr;
};

enum class Scene { Other, Battle, Result };

// The result bar "WIN 1-0 LOSE": white WIN in one slot, grey LOSE on dark in the other.
bool result_bar_likely(const Frame &f);

// The round timer: big purple digits at the top centre.
bool battle_hud_likely(const Frame &f);

// One look at one picture.
Scene classify(const Frame &f);

// Mean absolute difference (0-255) between two pictures of the same size, sampled on a grid.
// Used to skip work while the picture stands still.
double frame_difference(const Frame &a, const Frame &b, int step = 8);

const char *scene_name(Scene s);

} // namespace sf6
