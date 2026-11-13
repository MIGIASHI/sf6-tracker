// "SF6 Tracker" video filter: put it on the game capture and it looks at the picture from inside OBS,
// with no WebSocket and no separate program.
//
// The picture is passed through untouched. A few times a second the filter also draws the source
// into a small 640x360 texture and copies it to the CPU one frame later (so the render thread never
// waits for the GPU). The looking happens on a worker thread at below-normal priority, so a slow
// look can never hold up the stream.

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <util/threading.h>
#include <plugin-support.h>

#include "scene.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;
constexpr int kDefaultInterval = 250;
// A scene has to be seen this many looks in a row before it counts, so one odd picture
// (a flash, a menu that happens to look alike) does not flip it.
constexpr int kStableLooks = 2;

struct Filter {
	obs_source_t *context = nullptr;

	// Render thread only.
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stage = nullptr;
	bool staged = false;
	uint64_t last_grab_ns = 0;
	std::atomic<uint64_t> interval_ns{uint64_t(kDefaultInterval) * 1000000ULL};

	// Hand-over from the render thread to the worker.
	std::mutex mutex;
	std::condition_variable wake;
	std::vector<uint8_t> incoming;
	bool has_incoming = false;
	bool stopping = false;
	std::thread worker;

	// Worker thread only.
	std::vector<uint8_t> current, previous;
	sf6::Scene candidate = sf6::Scene::Other;
	int candidate_count = 0;

	// Read by the properties dialog.
	std::atomic<int> scene{-1}; // -1: no picture yet
	std::atomic<uint64_t> looks{0};
	std::atomic<uint64_t> skipped{0};
	std::atomic<uint64_t> results{0};
	std::atomic<double> last_look_ms{0.0};

	std::mutex saved_mutex;
	std::vector<uint8_t> saved; // the last picture, for "save the current picture"
};

sf6::Frame as_frame(const std::vector<uint8_t> &buf)
{
	sf6::Frame f;
	f.width = int(kWidth);
	f.height = int(kHeight);
	f.stride = int(kWidth * 4);
	f.data = buf.empty() ? nullptr : buf.data();
	return f;
}

const char *scene_text(int scene)
{
	switch (scene) {
	case int(sf6::Scene::Battle):
		return obs_module_text("Scene.Battle");
	case int(sf6::Scene::Result):
		return obs_module_text("Scene.Result");
	case int(sf6::Scene::Other):
		return obs_module_text("Scene.Other");
	default:
		return obs_module_text("Scene.None");
	}
}

void look(Filter *f)
{
	uint64_t start = os_gettime_ns();
	sf6::Frame now = as_frame(f->current);

	// A picture that has not changed cannot change the scene.
	if (!f->previous.empty() && sf6::frame_difference(now, as_frame(f->previous)) < 1.0) {
		f->skipped++;
		return;
	}
	f->previous.swap(f->current);
	now = as_frame(f->previous);

	sf6::Scene s = sf6::classify(now);
	if (s == f->candidate) {
		f->candidate_count++;
	} else {
		f->candidate = s;
		f->candidate_count = 1;
	}
	if (f->candidate_count == kStableLooks && int(s) != f->scene.load()) {
		int before = f->scene.exchange(int(s));
		if (s == sf6::Scene::Result)
			f->results++;
		obs_log(LOG_INFO, "[%s] scene: %s -> %s", obs_source_get_name(f->context),
			before < 0 ? "none" : sf6::scene_name(sf6::Scene(before)), sf6::scene_name(s));
	}

	f->looks++;
	f->last_look_ms = double(os_gettime_ns() - start) / 1e6;
	std::lock_guard<std::mutex> lock(f->saved_mutex);
	f->saved = f->previous;
}

void worker_main(Filter *f)
{
	os_set_thread_name("sf6-tracker-look");
#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	for (;;) {
		{
			std::unique_lock<std::mutex> lock(f->mutex);
			f->wake.wait(lock, [f] { return f->stopping || f->has_incoming; });
			if (f->stopping)
				return;
			f->current.swap(f->incoming);
			f->has_incoming = false;
		}
		look(f);
	}
}

// Copy last frame's staged picture to the worker. Called before staging a new one, one frame
// later, so the GPU has finished the copy and mapping does not stall.
void take_staged(Filter *f)
{
	if (!f->staged)
		return;
	f->staged = false;
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(f->stage, &data, &linesize))
		return;
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		f->incoming.resize(size_t(kWidth) * kHeight * 4);
		for (uint32_t y = 0; y < kHeight; ++y)
			std::memcpy(f->incoming.data() + size_t(y) * kWidth * 4, data + size_t(y) * linesize, kWidth * 4);
		f->has_incoming = true; // an unread older picture is simply replaced
	}
	gs_stagesurface_unmap(f->stage);
	f->wake.notify_one();
}

void grab(Filter *f)
{
	take_staged(f);

	uint64_t now = os_gettime_ns();
	if (now - f->last_grab_ns < f->interval_ns.load())
		return;

	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target)
		return;
	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);
	if (!cx || !cy)
		return;
	f->last_grab_ns = now;

	gs_texrender_reset(f->texrender);
	if (!gs_texrender_begin(f->texrender, kWidth, kHeight))
		return;
	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, float(cx), 0.0f, float(cy), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(f->texrender);

	gs_stage_texture(f->stage, gs_texrender_get_texture(f->texrender));
	f->staged = true;
}

// --- OBS callbacks -------------------------------------------------------------------------

const char *filter_name(void *)
{
	return obs_module_text("SF6Tracker");
}

void filter_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<Filter *>(data);
	long long ms = obs_data_get_int(settings, "interval");
	if (ms < 50)
		ms = 50;
	f->interval_ns = uint64_t(ms) * 1000000ULL;
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new Filter();
	f->context = source;

	obs_enter_graphics();
	f->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	f->stage = gs_stagesurface_create(kWidth, kHeight, GS_BGRA);
	obs_leave_graphics();

	filter_update(f, settings);
	f->worker = std::thread(worker_main, f);
	return f;
}

void filter_destroy(void *data)
{
	auto *f = static_cast<Filter *>(data);
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		f->stopping = true;
	}
	f->wake.notify_one();
	if (f->worker.joinable())
		f->worker.join();

	obs_enter_graphics();
	gs_stagesurface_destroy(f->stage);
	gs_texrender_destroy(f->texrender);
	obs_leave_graphics();
	delete f;
}

void filter_render(void *data, gs_effect_t *)
{
	auto *f = static_cast<Filter *>(data);
	obs_source_skip_video_filter(f->context);
	grab(f);
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "interval", kDefaultInterval);
}

std::string status_text(Filter *f)
{
	char buf[512];
	std::snprintf(buf, sizeof buf, "%s: %s\nlooks %llu / unchanged %llu / results %llu / last look %.1f ms",
		      obs_module_text("Status"), scene_text(f->scene.load()),
		      (unsigned long long)f->looks.load(), (unsigned long long)f->skipped.load(),
		      (unsigned long long)f->results.load(), f->last_look_ms.load());
	return buf;
}

bool write_bmp(const char *path, const std::vector<uint8_t> &bgra)
{
	FILE *fp = os_fopen(path, "wb");
	if (!fp)
		return false;
	const uint32_t size = kWidth * kHeight * 4;
	uint8_t h[54] = {'B', 'M'};
	auto put32 = [&h](int at, uint32_t v) {
		for (int i = 0; i < 4; ++i)
			h[at + i] = uint8_t(v >> (8 * i));
	};
	put32(2, 54 + size);
	put32(10, 54);
	put32(14, 40);
	put32(18, kWidth);
	put32(22, uint32_t(-int32_t(kHeight))); // negative height: rows top to bottom
	h[26] = 1;
	h[28] = 32;
	put32(34, size);
	bool ok = std::fwrite(h, 1, sizeof h, fp) == sizeof h && std::fwrite(bgra.data(), 1, size, fp) == size;
	std::fclose(fp);
	return ok;
}

bool on_refresh(obs_properties_t *, obs_property_t *, void *)
{
	return true; // rebuilds the dialog, which reads the status again
}

bool on_save(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = static_cast<Filter *>(data);
	std::vector<uint8_t> copy;
	{
		std::lock_guard<std::mutex> lock(f->saved_mutex);
		copy = f->saved;
	}
	if (copy.empty())
		return false;
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path("frame.bmp");
	if (!path)
		return false;
	if (write_bmp(path, copy))
		obs_log(LOG_INFO, "saved the current picture to %s", path);
	else
		obs_log(LOG_WARNING, "could not save the picture to %s", path);
	bfree(path);
	return false;
}

obs_properties_t *filter_properties(void *data)
{
	auto *f = static_cast<Filter *>(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int(props, "interval", obs_module_text("Interval"), 100, 2000, 50);
	if (f) {
		std::string text = status_text(f);
		obs_properties_add_text(props, "status", text.c_str(), OBS_TEXT_INFO);
	}
	obs_properties_add_button(props, "refresh", obs_module_text("Refresh"), on_refresh);
	obs_properties_add_button(props, "save", obs_module_text("SaveFrame"), on_save);
	return props;
}

} // namespace

extern "C" void sf6_register_filter(void)
{
	obs_source_info info = {};
	info.id = "sf6_tracker_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = filter_name;
	info.create = filter_create;
	info.destroy = filter_destroy;
	info.update = filter_update;
	info.get_defaults = filter_defaults;
	info.get_properties = filter_properties;
	info.video_render = filter_render;
	obs_register_source(&info);
}
