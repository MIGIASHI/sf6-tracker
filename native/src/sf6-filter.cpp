// "SF6 Tracker" video filter: put it on the SF6 game capture and the whole tracker runs inside OBS,
// with no WebSocket and no separate program. The overlay and the control panel are served by the
// plugin itself at http://127.0.0.1:17860/ (the same pages and addresses as the Python version).
//
// The picture is passed through untouched. When the tracker asks for a picture (every 0.3-1.5 s,
// like the Python version), the filter draws the source into a 1280x720 texture and copies it to
// the CPU one frame later, so the render thread never waits for the GPU. All reading happens on
// the tracker's below-normal worker thread.

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "tracker.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace {

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

std::mutex g_mutex;
std::unique_ptr<sf6::Tracker> g_tracker;
std::atomic<void *> g_feeder{nullptr}; // the filter that currently hands pictures over

std::string module_file(const char *name)
{
	char *p = obs_module_file(name);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

sf6::Tracker *tracker()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_tracker) {
		sf6::TrackerPaths paths;
		char *cfg = obs_module_config_path("");
		paths.data_dir = cfg ? cfg : "";
		bfree(cfg);
		paths.web_dir = module_file("web");
		paths.ort_library = module_file(
#ifdef _WIN32
			"ort/sf6-onnxruntime.dll"
#elif defined(__APPLE__)
			"ort/libonnxruntime.dylib"
#else
			"ort/libonnxruntime.so"
#endif
		);
		paths.det_model = module_file("models/ch_PP-OCRv4_det_infer.onnx");
		paths.rec_model = module_file("models/ch_PP-OCRv4_rec_infer.onnx");
		g_tracker = std::make_unique<sf6::Tracker>(paths);
		if (g_tracker->start())
			obs_log(LOG_INFO, "tracker running: http://127.0.0.1:%d/control", g_tracker->port());
		if (!g_tracker->error().empty())
			obs_log(LOG_WARNING, "%s", g_tracker->error().c_str());
	}
	return g_tracker.get();
}

struct Filter {
	obs_source_t *context = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stage = nullptr;
	bool staged = false;
	sf6::Tracker *tracker = nullptr;
};

void take_staged(Filter *f)
{
	if (!f->staged)
		return;
	f->staged = false;
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(f->stage, &data, &linesize))
		return;
	f->tracker->put_frame(data, int(kWidth), int(kHeight), int(linesize));
	gs_stagesurface_unmap(f->stage);
}

void grab(Filter *f)
{
	take_staged(f);
	if (!f->tracker || !f->tracker->wants_frame())
		return;
	// With the filter on two sources, the one that showed a picture last keeps feeding.
	void *cur = g_feeder.load();
	if (cur && cur != f && obs_source_showing(static_cast<Filter *>(cur)->context))
		return;
	g_feeder = f;

	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target)
		return;
	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);
	if (!cx || !cy)
		return;

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

const char *filter_name(void *)
{
	return obs_module_text("SF6Tracker");
}

void *filter_create(obs_data_t *, obs_source_t *source)
{
	auto *f = new Filter();
	f->context = source;
	f->tracker = tracker();
	obs_enter_graphics();
	f->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	f->stage = gs_stagesurface_create(kWidth, kHeight, GS_BGRA);
	obs_leave_graphics();
	return f;
}

void filter_destroy(void *data)
{
	auto *f = static_cast<Filter *>(data);
	void *expected = f;
	g_feeder.compare_exchange_strong(expected, nullptr);
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

bool on_refresh(obs_properties_t *, obs_property_t *, void *)
{
	return true; // rebuilds the dialog, which reads the status again
}

obs_properties_t *filter_properties(void *data)
{
	auto *f = static_cast<Filter *>(data);
	obs_properties_t *props = obs_properties_create();
	if (f && f->tracker) {
		sf6::json st = f->tracker->status();
		sf6::json d = st["debug"];
		std::string state = d.is_object() && d.contains("state") ? d["state"].get<std::string>() : "idle";
		const char *state_text = state == "result"   ? obs_module_text("State.Result")
					 : state == "match" ? obs_module_text("State.Match")
							    : obs_module_text("State.Idle");
		std::string text = std::string(obs_module_text("Status")) + ": " + state_text + "\n" +
				   st["capture"].get<std::string>();
		if (st["error"].is_string())
			text += "\n" + st["error"].get<std::string>();
		obs_properties_add_text(props, "status", text.c_str(), OBS_TEXT_INFO);
		std::string port = std::to_string(f->tracker->port());
		std::string urls = std::string(obs_module_text("Control")) + ": http://127.0.0.1:" + port + "/control\n" +
				   obs_module_text("Overlay") + ": http://127.0.0.1:" + port + "/overlay";
		obs_properties_add_text(props, "urls", urls.c_str(), OBS_TEXT_INFO);
	}
	obs_properties_add_button(props, "refresh", obs_module_text("Refresh"), on_refresh);
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
	info.get_properties = filter_properties;
	info.video_render = filter_render;
	obs_register_source(&info);
}

extern "C" void sf6_shutdown(void)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_tracker)
		g_tracker->stop();
	g_tracker.reset();
}
