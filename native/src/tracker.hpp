// The running tracker inside OBS: takes pictures from the filter, runs the Detector on a
// below-normal worker thread at the Python tracker's pace, and answers the web pages.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "detector.hpp"

namespace sf6 {

struct TrackerPaths {
	std::string data_dir;    // settings and history (per user)
	std::string web_dir;     // control.html, overlay.html, input.html
	std::string ort_library; // ONNX Runtime
	std::string det_model, rec_model;
};

class HttpServer;

class Tracker {
public:
	explicit Tracker(TrackerPaths paths);
	~Tracker();

	bool start(int port_override = 0); // loads the models, starts the worker and the web server
	void stop();

	// Filter side: is a new picture wanted now? Then hand one over (BGRA, any size).
	bool wants_frame() const { return want_.load(); }
	void put_frame(const uint8_t *bgra, int width, int height, int stride);

	// Web side.
	json state(const std::string &scope);
	json status();
	json config_with_roster();
	void update_config(const json &patch);
	json matches_decorated();
	json add_match(json fields);
	std::optional<json> update_match(int id, const json &fields);
	bool delete_match(int id);
	int new_session();
	json test_frame(const json &seq);
	json frame_list();
	std::string frame_bmp(long long seq); // -1 = latest; "" when not there
	std::string csv();
	const TrackerPaths &paths() const { return paths_; }
	int port() const { return port_; }
	std::string error() const;

private:
	void loop();
	json decorate(const json &m) const;

	TrackerPaths paths_;
	Config cfg_;
	Store store_;
	Ocr ocr_;
	std::unique_ptr<Detector> det_;
	std::unique_ptr<HttpServer> http_;
	int port_ = 0;

	std::atomic<bool> running_{false};
	std::atomic<bool> want_{false};
	std::thread worker_;
	std::mutex frame_mutex_;
	std::condition_variable frame_cv_;
	std::vector<uint8_t> incoming_;
	int in_w_ = 0, in_h_ = 0;
	bool has_incoming_ = false;
	std::atomic<double> last_frame_time_{0};

	std::mutex det_mutex_; // the Detector and the pictures kept for the pages
	struct Kept {
		long long seq;
		double ts;
		Image small; // 640 wide, for the calibration screen
	};
	std::deque<Kept> kept_;
	Image last_full_;
	std::vector<uint8_t> last_bgra_;
	long long seq_ = 0;
	std::atomic<int> tick_ms_{0};
	mutable std::mutex err_mutex_;
	std::string error_;
};

std::string encode_bmp(const Image &img);

} // namespace sf6
