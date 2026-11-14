// Match history and settings, kept in the same JSON files and shapes as the Python tracker
// (history.json, config.json), so the web pages work unchanged and the history can move between them.
#pragma once

#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sf6 {

using nlohmann::json;

double now_seconds();
json jget(const json &obj, const char *key); // obj[key], or null when missing

class Store {
public:
	explicit Store(std::string data_dir);

	int session_id() const;
	json matches() const;
	std::optional<json> get(int id) const;
	std::optional<json> last() const;
	json add(const json &fields);
	std::optional<json> update(int id, const json &fields);
	bool remove(int id);
	void resolve_pending(const std::string &pending_id, const std::string &char_id);
	int new_session();
	std::string csv() const;

private:
	void save();
	static json pick(const json &fields);

	std::string path_;
	mutable std::recursive_mutex mutex_;
	json data_;
};

// Numbers for the overlay from a list of matches (oldest first), like store.summarize.
json summarize(const json &matches);

class Config {
public:
	explicit Config(std::string data_dir);
	json get() const;
	json section(const char *name) const;
	void update(const json &patch);

private:
	void save();
	std::string path_;
	mutable std::recursive_mutex mutex_;
	json data_;
};

json default_config();

// Reads / writes a whole file; paths are UTF-8.
bool read_text_file(const std::string &path, std::string &out);
bool write_text_file_atomic(const std::string &path, const std::string &text);
void make_dirs(const std::string &dir);

} // namespace sf6
