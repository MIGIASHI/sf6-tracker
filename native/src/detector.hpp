// Turns a stream of pictures into match records: VS screen -> battle HUD -> result bar.
// A port of the SF6 layout path of sf6tracker/detector.py (Detector._process_sf6 and its helpers),
// with the same places on screen, the same votes and the same timings.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "image.hpp"
#include "ocr.hpp"
#include "scene.hpp"
#include "store.hpp"

namespace sf6 {

// collections.Counter with most_common(1): highest count, ties go to the first one seen.
template<typename K> class Counter {
public:
	void add(const K &k, int n = 1)
	{
		for (auto &e : items_)
			if (e.first == k) {
				e.second += n;
				return;
			}
		items_.push_back({k, n});
	}
	bool empty() const { return items_.empty(); }
	void clear() { items_.clear(); }
	std::optional<K> top(int need = 1) const
	{
		const std::pair<K, int> *best = nullptr;
		for (const auto &e : items_)
			if (!best || e.second > best->second)
				best = &e;
		if (best && best->second >= need)
			return best->first;
		return std::nullopt;
	}
	const std::vector<std::pair<K, int>> &items() const { return items_; }

private:
	std::vector<std::pair<K, int>> items_;
};

using RatingKey = std::pair<int, std::string>; // (value, "MR" / "LP")

class StriveDetector {
public:
	StriveDetector(Config &cfg, Store &store, Ocr &ocr) : cfg_(cfg), store_(store), ocr_(ocr) {}
	void process(const Image &full, double now);
	json inspect(const Image &full);
	json debug() const { return debug_; }
	double hurry() const { return (result_visible_ && !recorded_) || reading_intro_ ? 0.35 : 0; }

private:
	void read_players(const Image &full, double now);
	Config &cfg_;
	Store &store_;
	Ocr &ocr_;
	json players_;
	double players_seen_ = 0, next_players_ = 0;
	json candidate_;
	json debug_ = json::object();
	int confirmations_ = 0, absent_ = 0;
	double last_seen_ = 0;
	bool result_visible_ = false, recorded_ = false;
	bool reading_intro_ = false;
	std::optional<int> delta_;
};

// One picture plus what was read from it, so each place is read at most once.
class FrameContext {
public:
	FrameContext(const Frame &look, const Image &full, Ocr &ocr, const FrameContext *previous);
	const Frame &look() const { return look_; }
	const Items &region(const std::string &name);
	std::vector<uint8_t> thumb; // 48x27 grey, to notice a picture that has not changed

private:
	Frame look_;
	const Image &full_;
	Ocr &ocr_;
	std::map<std::string, Items> texts_;
	friend class Detector;
};

class Detector {
public:
	Detector(Config &cfg, Store &store, Ocr &ocr);

	// Looks at one picture. Returns the match that was just recorded, if any.
	std::optional<json> process(const Frame &look, const Image &full, double now);
	// What a single picture shows, for the "test" button.
	json inspect(const Frame &look, const Image &full);

	// The input display saw n Drive Rush inputs (0 = only saying it is counting). Counted during a battle only.
	bool add_rush(int n, double now);

	double hurry() const { return hurry_; } // seconds until the next look when it must come sooner (0 = no hurry)
	json debug() const { return debug_; }

private:
	struct Match {
		double seen = 0;
		std::optional<double> vs_last;
		int vs_reads = 0, hud_reads = 0;
		double hud_next = 0;
		bool linked = false;
		Counter<std::string> chars[2];
		Counter<RatingKey> rating[2], changed[2];
		Counter<std::string> side;
		int you = 0, named = 0;
		double hud_seen = 0;
		int rush = 0;
	};
	struct Active {
		int id = 0;
		double start = 0, seen = 0;
		std::vector<RatingKey> readings;
		std::string side, label; // label "" = not known
		std::optional<int> before, prev;
		bool settled = false;
	};
	struct Recovery {
		json fields, last_id;
		RatingKey before;
		int session = 0, confirmations = 0;
		double started = 0;
		std::optional<RatingKey> candidate;
	};

	std::vector<std::string> names() const;
	std::string wanted_label() const; // "MR" / "LP" when fixed in the settings, "" for automatic
	int dint(const char *key, int def) const;

	static Match new_match(double now);
	void take(Match &m, const Items &items, const char *side);
	void result_clues(FrameContext &ctx);
	bool vs_screen(FrameContext &ctx, double now);
	std::pair<std::string, std::string> side_now() const;
	bool complete(const Match &m) const;
	void settle_previous(const std::optional<RatingKey> &r, const std::string &my_char, double now);
	json record(const std::string &p1res, double now);
	void result_rating(FrameContext &ctx, double now);
	void arm_recovery();
	void track_character();
	std::optional<json> recover_rating(const Match &probe, double now);

	Config &cfg_;
	Store &store_;
	Ocr &ocr_;
	json c_; // settings snapshot for this look

	std::optional<Match> m_;
	std::optional<Active> active_;
	std::optional<Recovery> recovery_;
	int recovery_session_ = 0;
	Counter<std::string> res_side_;
	std::string res_label_;
	std::pair<std::string, int> streak_{"", 0};
	std::string last_side_;
	double last_record_ = 0, card_next_ = 0, vs_next_ = 0, hurry_ = 0, rush_seen_ = 0;
	std::unique_ptr<FrameContext> last_ctx_;
	Image last_full_;
	json debug_ = json::object();
};

} // namespace sf6
