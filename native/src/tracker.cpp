#include "tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <regex>

#include "http.hpp"
#include "text.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sf6 {

namespace {

std::string day_of(double ts)
{
	std::time_t t = std::time_t(ts);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char buf[16];
	std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
	return buf;
}

HttpResponse json_response(const json &j, int code = 200)
{
	HttpResponse r;
	r.code = code;
	r.body = j.dump(-1, ' ', false, json::error_handler_t::replace);
	return r;
}

bool local_host(const std::string &host_header)
{
	std::string host = host_header;
	if (!host.empty() && host[0] == '[')
		return false;
	size_t colon = host.rfind(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);
	return host == "127.0.0.1" || host == "localhost";
}

bool local_origin(const std::string &origin)
{
	// "http://127.0.0.1:17860" -> host part
	size_t s = origin.find("://");
	std::string rest = s == std::string::npos ? origin : origin.substr(s + 3);
	return local_host(rest.substr(0, rest.find('/')));
}

} // namespace

std::string encode_bmp(const Image &img)
{
	const int row = (img.w * 3 + 3) & ~3;
	const uint32_t size = uint32_t(row) * img.h;
	std::string out(54 + size, '\0');
	auto put32 = [&out](size_t at, uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out[at + i] = char(v >> (8 * i));
	};
	out[0] = 'B';
	out[1] = 'M';
	put32(2, 54 + size);
	put32(10, 54);
	put32(14, 40);
	put32(18, uint32_t(img.w));
	put32(22, uint32_t(-img.h)); // rows top to bottom
	out[26] = 1;
	out[28] = 24;
	put32(34, size);
	for (int y = 0; y < img.h; ++y)
		std::copy(img.at(0, y), img.at(0, y) + size_t(img.w) * 3, out.begin() + 54 + ptrdiff_t(y) * row);
	return out;
}

Tracker::Tracker(TrackerPaths paths) : paths_(std::move(paths)), cfg_(paths_.data_dir), store_(paths_.data_dir) {}

Tracker::~Tracker()
{
	stop();
}

std::string Tracker::error() const
{
	std::lock_guard<std::mutex> lock(err_mutex_);
	return error_;
}

bool Tracker::start(int port_override)
{
	if (running_)
		return true;
	if (!ocr_.load(paths_.ort_library, paths_.det_model, paths_.rec_model)) {
		std::lock_guard<std::mutex> lock(err_mutex_);
		error_ = "文字認識を読み込めません: " + ocr_.error();
	}
	det_ = std::make_unique<Detector>(cfg_, store_, ocr_);
	port_ = port_override ? port_override : jget(cfg_.section("server"), "port").get<int>();

	http_ = std::make_unique<HttpServer>([this](const HttpRequest &req) -> HttpResponse {
		// Only answer when addressed as this PC (stops other sites from reaching us by a DNS trick).
		auto h = req.headers.find("host");
		if (h == req.headers.end() || !local_host(h->second))
			return json_response({{"error", "forbidden"}}, 403);
		const std::string &p = req.path;
		if (req.method == "GET") {
			static const std::map<std::string, std::string> pages = {{"/", "control.html"},
										  {"/control", "control.html"},
										  {"/overlay", "overlay.html"},
										  {"/input", "input.html"}};
			auto page = pages.find(p);
			if (page != pages.end()) {
				HttpResponse r;
				std::string path = (std::filesystem::u8path(paths_.web_dir) / page->second).u8string();
				if (!read_text_file(path, r.body))
					return json_response({{"error", "page missing: " + path}}, 404);
				r.type = "text/html; charset=utf-8";
				return r;
			}
			if (p == "/api/state")
				return json_response(state(req.param("scope").empty() ? "session" : req.param("scope")));
			if (p == "/api/status")
				return json_response(status());
			if (p == "/api/input")
				return json_response(input_.snapshot());
			if (p == "/api/matches")
				return json_response(matches_decorated());
			if (p == "/api/config")
				return json_response(config_with_roster());
			if (p == "/api/sources")
				return json_response(json::array());
			if (p == "/api/frames")
				return json_response(frame_list());
			std::smatch m;
			static const std::regex frame_re(R"(/api/frame/(latest|\d+)\.jpg)");
			if (std::regex_match(p, m, frame_re)) {
				std::string bmp = frame_bmp(m[1] == "latest" ? -1 : std::stoll(m[1]));
				if (bmp.empty())
					return json_response({{"error", "no frame"}}, 404);
				HttpResponse r;
				r.type = "image/bmp";
				r.body = std::move(bmp);
				return r;
			}
			if (p == "/api/export.csv") {
				HttpResponse r;
				r.type = "text/csv; charset=utf-8";
				r.body = "\xEF\xBB\xBF" + csv();
				r.headers["Content-Disposition"] = "attachment; filename=\"sf6_history.csv\"";
				return r;
			}
			return json_response({{"error", "not found"}}, 404);
		}
		if (req.method != "POST")
			return json_response({{"error", "not found"}}, 404);
		// Browsers may only call this from our own pages.
		auto o = req.headers.find("origin");
		if (o != req.headers.end() && !o->second.empty() && o->second != "null" && !local_origin(o->second))
			return json_response({{"error", "forbidden"}}, 403);
		if (p == "/api/frames/upload")
			return json_response({{"error", "ネイティブ版では画像の読み込みはまだできません"}}, 400);
		json data = json::object();
		if (!req.body.empty()) {
			try {
				data = json::parse(req.body);
			} catch (const std::exception &) {
				return json_response({{"error", "bad json"}}, 400);
			}
		}
		if (p == "/api/config") {
			if (data.is_object())
				data.erase("roster");
			update_config(data);
			return json_response({{"ok", true}});
		}
		if (p == "/api/matches")
			return json_response(add_match(data));
		std::smatch m;
		static const std::regex match_re(R"(/api/matches/(\d+)(/delete)?)");
		if (std::regex_match(p, m, match_re)) {
			int id = std::stoi(m[1]);
			if (m[2].matched)
				return json_response({{"ok", delete_match(id)}});
			auto r = update_match(id, data);
			return r ? json_response(*r) : json_response({{"error", "not found"}}, 404);
		}
		if (p == "/api/shutdown") // the Python version stops here; inside OBS we keep running
			return json_response({{"ok", true}, {"native", true}});
		if (p == "/api/rush") { // the input display counting Drive Rush inputs
			json n = jget(data, "n");
			int k = n.is_number() ? std::clamp(int(n.get<double>()), 0, 10) : 0;
			return json_response({{"ok", true}, {"counted", add_rush(k)}});
		}
		if (p == "/api/session/new")
			return json_response({{"session", new_session()}});
		if (p == "/api/test")
			return json_response(test_frame(jget(data, "seq")));
		if (p == "/api/template" || p == "/api/template/clear" || p == "/api/pending/label")
			return json_response({{"ok", false}});
		return json_response({{"error", "not found"}}, 404);
	});
	std::string err;
	if (!http_->start(port_, err)) {
		std::lock_guard<std::mutex> lock(err_mutex_);
		error_ = err;
		http_.reset();
	}
	running_ = true;
	input_.start();
	worker_ = std::thread(&Tracker::loop, this);
	return http_ != nullptr;
}

void Tracker::stop()
{
	if (!running_.exchange(false))
		return;
	frame_cv_.notify_all();
	if (worker_.joinable())
		worker_.join();
	if (http_)
		http_->stop();
	http_.reset();
	input_.stop();
}

void Tracker::put_frame(const uint8_t *bgra, int width, int height, int stride)
{
	{
		std::lock_guard<std::mutex> lock(frame_mutex_);
		incoming_.resize(size_t(width) * height * 4);
		for (int y = 0; y < height; ++y)
			std::copy(bgra + size_t(y) * stride, bgra + size_t(y) * stride + size_t(width) * 4,
				  incoming_.begin() + ptrdiff_t(y) * width * 4);
		in_w_ = width;
		in_h_ = height;
		has_incoming_ = true;
		want_ = false;
	}
	last_frame_time_ = now_seconds();
	frame_cv_.notify_all();
}

void Tracker::loop()
{
#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	std::vector<uint8_t> bgra;
	while (running_) {
		auto t0 = std::chrono::steady_clock::now();
		double interval;
		{
			std::lock_guard<std::mutex> lock(det_mutex_);
			json cap = cfg_.section("capture");
			double cfg_interval = cap.is_object() && cap.contains("interval") ? cap["interval"].get<double>() : 1.5;
			interval = det_->hurry() > 0 ? det_->hurry() : cfg_interval;
		}
		// Ask the filter for a picture and wait for it (no picture: OBS not showing the source).
		int w = 0, h = 0;
		{
			std::unique_lock<std::mutex> lock(frame_mutex_);
			want_ = true;
			frame_cv_.wait_for(lock, std::chrono::seconds(1), [this] { return has_incoming_ || !running_; });
			if (!running_)
				break;
			if (has_incoming_) {
				bgra.swap(incoming_);
				w = in_w_;
				h = in_h_;
				has_incoming_ = false;
			}
		}
		if (w > 0) {
			t0 = std::chrono::steady_clock::now(); // waiting for OBS to draw is not work
			try {
				Frame look{w, h, w * 4, bgra.data()};
				Image full = from_frame(look);
				double now = now_seconds();
				std::lock_guard<std::mutex> lock(det_mutex_);
				det_->process(look, full, now);
				kept_.push_back({++seq_, now, resize_area(full, 640, std::max(1, full.h * 640 / std::max(1, full.w)))});
				while (kept_.size() > 60)
					kept_.pop_front();
				last_full_ = std::move(full);
				std::lock_guard<std::mutex> elock(err_mutex_);
				if (ocr_.ready())
					error_.clear();
			} catch (const std::exception &e) { // keep the stream tool alive whatever happens
				std::lock_guard<std::mutex> lock(err_mutex_);
				error_ = e.what();
			}
		}
		double busy = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (w > 0)
			tick_ms_ = int(busy * 1000);
		else
			busy = 0;
		// Never work more than a third of the time, however slow one look gets.
		double wait = std::max({2.0 * busy, interval - busy, 0.1});
		std::unique_lock<std::mutex> lock(frame_mutex_);
		frame_cv_.wait_for(lock, std::chrono::duration<double>(wait), [this] { return !running_.load(); });
	}
}

json Tracker::decorate(const json &m) const
{
	json d = m;
	json lang = jget(cfg_.section("display"), "char_lang");
	std::string l = lang.is_string() ? lang.get<std::string>() : "ja";
	json mc = jget(m, "my_char"), oc = jget(m, "opp_char");
	d["my_name"] = char_name(mc.is_string() ? mc.get<std::string>() : "", l);
	d["opp_name"] = char_name(oc.is_string() ? oc.get<std::string>() : "", l);
	return d;
}

json Tracker::state(const std::string &scope)
{
	json all = store_.matches();
	json ms = json::array();
	if (scope == "today") {
		std::string today = day_of(now_seconds());
		for (const auto &m : all)
			if (day_of(m.value("ts", 0.0)) == today)
				ms.push_back(m);
	} else if (scope == "all") {
		ms = all;
	} else {
		int sid = store_.session_id();
		for (const auto &m : all)
			if (jget(m, "session") == json(sid))
				ms.push_back(m);
	}
	json s = summarize(ms);
	if (s["rating"].is_null()) // nothing rated in this scope yet: still show the last known rating
		for (auto it = all.rbegin(); it != all.rend(); ++it)
			if (!jget(*it, "rating").is_null()) {
				s["rating"] = (*it)["rating"];
				break;
			}
	// the label read off the screen (LP below Master, MR above) wins over the one in the settings
	json label = nullptr;
	for (auto it = all.rbegin(); it != all.rend(); ++it)
		if (!jget(*it, "rating").is_null() && jget(*it, "rating_label").is_string() &&
		    !(*it)["rating_label"].get<std::string>().empty()) {
			label = (*it)["rating_label"];
			break;
		}
	if (label.is_null()) {
		json fixed = jget(cfg_.section("player"), "rating_label");
		label = (fixed == "MR" || fixed == "LP") ? fixed : json("MR");
	}
	s["label"] = label;
	s["scope"] = scope;
	json list = json::array();
	for (auto it = ms.rbegin(); it != ms.rend() && list.size() < 50; ++it)
		list.push_back(decorate(*it));
	s["matches"] = list;
	return s;
}

json Tracker::status()
{
	double last = last_frame_time_.load();
	std::string capture;
	if (last <= 0)
		capture = "ネイティブ版: まだ画面が来ていません（ゲームキャプチャにフィルター「SF6 Tracker」を付けてください）";
	else if (now_seconds() - last > 5)
		capture = "ネイティブ版: 画面が止まっています（ソースが非表示か、シーンに出ていません）";
	else
		capture = "ネイティブ版: OBS の画面を見ています";
	json debug;
	size_t frames;
	{
		std::lock_guard<std::mutex> lock(det_mutex_);
		debug = det_ ? det_->debug() : json::object();
		frames = kept_.size();
	}
	std::string err = error();
	return {{"capture", capture},
		{"error", err.empty() ? json(nullptr) : json(err)},
		{"tick_ms", tick_ms_.load()},
		{"debug", debug},
		{"frames", frames},
		{"session", store_.session_id()},
		{"pending", json::array()},
		{"file_mode", false},
		{"native", true}};
}

json Tracker::config_with_roster()
{
	json c = cfg_.get();
	json lang = jget(c["display"], "char_lang");
	std::string l = lang.is_string() ? lang.get<std::string>() : "ja";
	json r = json::array();
	for (const auto &ch : roster())
		r.push_back({{"id", ch.id}, {"name", l == "ja" ? ch.ja : ch.en}});
	c["roster"] = r;
	return c;
}

void Tracker::update_config(const json &patch)
{
	cfg_.update(patch);
}

json Tracker::matches_decorated()
{
	json all = store_.matches(), out = json::array();
	for (auto it = all.rbegin(); it != all.rend(); ++it)
		out.push_back(decorate(*it));
	return out;
}

json Tracker::add_match(json fields)
{
	if (!fields.is_object())
		fields = json::object();
	if (!fields.contains("ts"))
		fields["ts"] = now_seconds();
	fields["auto"] = false;
	return decorate(store_.add(fields));
}

std::optional<json> Tracker::update_match(int id, const json &fields)
{
	auto r = store_.update(id, fields);
	if (r)
		return decorate(*r);
	return r;
}

bool Tracker::delete_match(int id)
{
	return store_.remove(id);
}

bool Tracker::add_rush(int n)
{
	std::lock_guard<std::mutex> lock(det_mutex_);
	return det_ && det_->add_rush(n, now_seconds());
}

int Tracker::new_session()
{
	return store_.new_session();
}

json Tracker::test_frame(const json &seq)
{
	std::lock_guard<std::mutex> lock(det_mutex_);
	Image full;
	if (seq.is_number() && (kept_.empty() || seq.get<long long>() != kept_.back().seq)) {
		for (const auto &k : kept_)
			if (k.seq == seq.get<long long>())
				full = resize_linear(k.small, k.small.w * 2, k.small.h * 2);
	} else {
		full = last_full_;
	}
	if (full.empty())
		return {{"error", "フレームがありません"}};
	std::vector<uint8_t> bgra(size_t(full.w) * full.h * 4, 255);
	for (size_t i = 0, n = size_t(full.w) * full.h; i < n; ++i)
		for (int c = 0; c < 3; ++c)
			bgra[i * 4 + c] = full.px[i * 3 + c];
	Frame look{full.w, full.h, full.w * 4, bgra.data()};
	json out = det_->inspect(look, full);
	json lang = jget(cfg_.section("display"), "char_lang");
	std::string l = lang.is_string() ? lang.get<std::string>() : "ja";
	for (const char *s : {"p1", "p2"}) {
		json id = out["chars"][s];
		out["chars"][s] = {{"id", id}, {"name", id.is_string() ? json(char_name(id, l)) : json(nullptr)}};
	}
	out["texts"] = json::object();
	return out;
}

json Tracker::frame_list()
{
	std::lock_guard<std::mutex> lock(det_mutex_);
	json out = json::array();
	for (const auto &k : kept_)
		out.push_back({{"seq", k.seq}, {"ts", k.ts}});
	return out;
}

std::string Tracker::frame_bmp(long long seq)
{
	std::lock_guard<std::mutex> lock(det_mutex_);
	if (kept_.empty())
		return "";
	if (seq < 0)
		return encode_bmp(kept_.back().small);
	for (const auto &k : kept_)
		if (k.seq == seq)
			return encode_bmp(k.small);
	return "";
}

std::string Tracker::csv()
{
	return store_.csv();
}

} // namespace sf6
