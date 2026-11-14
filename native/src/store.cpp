#include "store.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace sf6 {

namespace {

const char *const kFields[] = {"ts",          "result",     "my_char", "opp_char", "rating", "delta",
			       "rating_label", "my_pending", "opp_pending", "auto", "carried", "note",
			       "side",         "side_by",    "rating_before"};

fs::path to_path(const std::string &utf8)
{
	return fs::u8path(utf8);
}

void merge(json &base, const json &over)
{
	for (auto it = over.begin(); it != over.end(); ++it) {
		if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object())
			merge(base[it.key()], it.value());
		else
			base[it.key()] = it.value();
	}
}

std::string csv_field(const std::string &s)
{
	if (s.find_first_of(",\"\r\n") == std::string::npos)
		return s;
	std::string out = "\"";
	for (char c : s) {
		if (c == '"')
			out += '"';
		out += c;
	}
	return out + "\"";
}

std::string text_of(const json &v)
{
	if (v.is_null())
		return "";
	if (v.is_string())
		return v.get<std::string>();
	if (v.is_number_integer())
		return std::to_string(v.get<long long>());
	return v.dump();
}

} // namespace

double now_seconds()
{
	using namespace std::chrono;
	return duration<double>(system_clock::now().time_since_epoch()).count();
}

json jget(const json &obj, const char *key)
{
	if (obj.is_object()) {
		auto it = obj.find(key);
		if (it != obj.end())
			return *it;
	}
	return nullptr;
}

bool read_text_file(const std::string &path, std::string &out)
{
	std::ifstream f(to_path(path), std::ios::binary);
	if (!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

bool write_text_file_atomic(const std::string &path, const std::string &text)
{
	fs::path p = to_path(path), tmp = to_path(path + ".tmp");
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		f << text;
		if (!f)
			return false;
	}
	std::error_code ec;
	fs::rename(tmp, p, ec);
	return !ec;
}

void make_dirs(const std::string &dir)
{
	std::error_code ec;
	fs::create_directories(to_path(dir), ec);
}

// ---- Store ------------------------------------------------------------------

Store::Store(std::string data_dir) : path_((to_path(data_dir) / "history.json").u8string())
{
	data_ = {{"version", 1},
		 {"session", {{"id", 1}, {"started", now_seconds()}}},
		 {"next_id", 1},
		 {"matches", json::array()}};
	std::string text;
	if (read_text_file(path_, text)) {
		try {
			json loaded = json::parse(text);
			for (auto it = loaded.begin(); it != loaded.end(); ++it)
				data_[it.key()] = it.value();
		} catch (const std::exception &) {
			// A broken file is kept aside rather than overwritten.
			std::error_code ec;
			fs::copy_file(to_path(path_), to_path(path_ + ".broken"), fs::copy_options::overwrite_existing, ec);
		}
	}
}

json Store::pick(const json &fields)
{
	json out = json::object();
	for (const char *k : kFields)
		if (fields.contains(k))
			out[k] = fields[k];
	return out;
}

void Store::save()
{
	write_text_file_atomic(path_, data_.dump(1));
}

int Store::session_id() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return data_["session"].value("id", 1);
}

json Store::matches() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return data_["matches"];
}

std::optional<json> Store::get(int id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (const auto &m : data_["matches"])
		if (m.value("id", -1) == id)
			return m;
	return std::nullopt;
}

std::optional<json> Store::last() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (data_["matches"].empty())
		return std::nullopt;
	return data_["matches"].back();
}

json Store::add(const json &fields)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	json m = {{"id", data_["next_id"]}, {"session", session_id()}, {"ts", now_seconds()}, {"result", "win"},
		  {"my_char", nullptr},     {"opp_char", nullptr},       {"rating", nullptr},   {"delta", nullptr}};
	json f = pick(fields);
	for (auto it = f.begin(); it != f.end(); ++it)
		m[it.key()] = it.value();
	data_["next_id"] = data_["next_id"].get<int>() + 1;
	data_["matches"].push_back(m);
	auto &ms = data_["matches"];
	std::stable_sort(ms.begin(), ms.end(), [](const json &a, const json &b) {
		double ta = a.value("ts", 0.0), tb = b.value("ts", 0.0);
		return ta != tb ? ta < tb : a.value("id", 0) < b.value("id", 0);
	});
	save();
	return m;
}

std::optional<json> Store::update(int id, const json &fields)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto &m : data_["matches"]) {
		if (m.value("id", -1) != id)
			continue;
		json f = pick(fields);
		for (auto it = f.begin(); it != f.end(); ++it)
			m[it.key()] = it.value();
		save();
		return m;
	}
	return std::nullopt;
}

bool Store::remove(int id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto &ms = data_["matches"];
	size_t n = ms.size();
	json kept = json::array();
	for (const auto &m : ms)
		if (m.value("id", -1) != id)
			kept.push_back(m);
	ms = kept;
	save();
	return ms.size() < n;
}

void Store::resolve_pending(const std::string &pending_id, const std::string &char_id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto &m : data_["matches"])
		for (const char *side : {"my", "opp"}) {
			std::string k = std::string(side) + "_pending";
			if (jget(m, k.c_str()) == json(pending_id)) {
				m[std::string(side) + "_char"] = char_id.empty() ? json(nullptr) : json(char_id);
				m[k] = nullptr;
			}
		}
	save();
}

int Store::new_session()
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	data_["session"] = {{"id", session_id() + 1}, {"started", now_seconds()}};
	save();
	return session_id();
}

std::string Store::csv() const
{
	std::ostringstream out;
	out << "id,datetime,session,result,my_char,opp_char,rating_label,rating,delta,note\r\n";
	for (const auto &m : matches()) {
		std::time_t t = std::time_t(m.value("ts", 0.0));
		std::tm tm{};
#ifdef _WIN32
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char when[32];
		std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
		out << text_of(jget(m, "id")) << ',' << when << ',' << text_of(jget(m, "session")) << ','
		    << csv_field(text_of(jget(m, "result"))) << ',' << csv_field(text_of(jget(m, "my_char"))) << ','
		    << csv_field(text_of(jget(m, "opp_char"))) << ',' << csv_field(text_of(jget(m, "rating_label")))
		    << ',' << text_of(jget(m, "rating")) << ',' << text_of(jget(m, "delta")) << ','
		    << csv_field(text_of(jget(m, "note"))) << "\r\n";
	}
	return out.str();
}

json summarize(const json &matches)
{
	int wins = 0, losses = 0, streak = 0;
	for (const auto &m : matches) {
		wins += jget(m, "result") == "win";
		losses += jget(m, "result") == "lose";
	}
	for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
		int s = jget(*it, "result") == "win" ? 1 : -1;
		if (streak == 0 || (streak > 0) == (s > 0))
			streak += s;
		else
			break;
	}
	json series = json::array();
	json start = nullptr;
	json first = nullptr;
	for (const auto &m : matches)
		if (!jget(m, "rating").is_null()) {
			if (first.is_null())
				first = m;
			series.push_back(m["rating"]);
		}
	if (!first.is_null()) {
		int r = first["rating"].get<int>();
		start = jget(first, "delta").is_null() ? r : r - first["delta"].get<int>();
		if (start != series[0])
			series.insert(series.begin(), start);
	}
	json out = {{"wins", wins},        {"losses", losses}, {"total", wins + losses},
		    {"rate", nullptr},     {"streak", streak}, {"rating", series.empty() ? json(nullptr) : series.back()},
		    {"rating_start", start}, {"series", series}};
	if (wins + losses)
		out["rate"] = std::round(1000.0 * wins / (wins + losses)) / 10.0;
	out["rating_diff"] = series.empty() ? json(nullptr) : json(series.back().get<int>() - start.get<int>());
	return out;
}

// ---- Config -------------------------------------------------------------------

json default_config()
{
	// The keys of DEFAULT in sf6tracker/config.py that the native version uses or the pages show.
	return json::parse(R"({
  "capture": {"width": 1280, "look_width": 640, "interval": 1.5},
  "server": {"port": 17860},
  "player": {"names": [], "chars": [], "side": "p1", "rating_label": "auto"},
  "display": {"char_lang": "ja"},
  "rois": {"vs": null, "result": null, "rating": null, "p1_char": null, "p2_char": null, "p1_name": null, "p2_name": null},
  "signals": {
    "vs": {"roi": "vs", "keywords": ["VS"], "templates": []},
    "win": {"roi": "result", "keywords": ["WIN", "WINS", "WINNER", "VICTORY"], "templates": []},
    "lose": {"roi": "result", "keywords": ["LOSE", "LOSES", "LOSER", "DEFEAT"], "templates": []}
  },
  "detect": {"layout": "sf6", "confirm_ticks": 2, "cooldown": 45.0, "template_threshold": 0.8, "char_threshold": 0.8,
             "rating_min": 100, "rating_max": 99999, "carry_minutes": 10}
})");
}

Config::Config(std::string data_dir) : path_((to_path(data_dir) / "config.json").u8string())
{
	make_dirs(data_dir);
	data_ = default_config();
	std::string text;
	if (read_text_file(path_, text)) {
		try {
			merge(data_, json::parse(text));
		} catch (const std::exception &) {
		}
	}
}

json Config::get() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return data_;
}

json Config::section(const char *name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return jget(data_, name);
}

void Config::update(const json &patch)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (patch.is_object())
		merge(data_, patch);
	save();
}

void Config::save()
{
	write_text_file_atomic(path_, data_.dump(2));
}

} // namespace sf6
