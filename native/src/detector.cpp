#include "detector.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "text.hpp"

namespace sf6 {

namespace {

struct Region {
	Roi roi;
	double scale;
};

// Where SF6 (16:9) draws what the tracker needs. Same as SF6_REGIONS in sf6tracker/detector.py.
const std::map<std::string, Region> &regions()
{
	static const std::map<std::string, Region> r = {
		{"bar", {{0.40, 0.762, 0.20, 0.058}, 2.0}},     // result screen: "WIN 1-0 LOSE" (left word = 1P)
		{"res_p1", {{0.04, 0.772, 0.34, 0.085}, 1.5}},  // result screen: player name and my rating
		{"res_p2", {{0.62, 0.772, 0.34, 0.085}, 1.5}},
		{"player_p1", {{0.0, 0.352, 0.14, 0.06}, 2.0}}, // result screen: "PLAYER1" / "PLAYER2"
		{"player_p2", {{0.86, 0.352, 0.14, 0.06}, 2.0}},
		{"vs_p1", {{0.0, 0.905, 0.15, 0.05}, 2.0}},     // VS screen: "19760 LP" under the 1P rank
		{"vs_p2", {{0.85, 0.905, 0.15, 0.05}, 2.0}},
		{"vs_band", {{0.0, 0.745, 1.0, 0.215}, 1.5}},   // VS screen: character names, player names, ratings
		{"hud_p1", {{0.0, 0.012, 0.20, 0.118}, 1.5}},   // battle: character name, YOU, rating, player name
		{"hud_p2", {{0.80, 0.012, 0.20, 0.118}, 1.5}},
	};
	return r;
}

int idx(const std::string &side)
{
	return side == "p2" ? 1 : 0;
}

const char *other(const std::string &side)
{
	return side == "p1" ? "p2" : "p1";
}

std::string flip(const std::string &res)
{
	return res == "win" ? "lose" : "win";
}

std::optional<RatingKey> my_rating(const Counter<RatingKey> &votes, int need = 1)
{
	// Master players have both on screen (MR on the VS screen, LP in the HUD): MR matters then.
	Counter<RatingKey> mr;
	for (const auto &e : votes.items())
		if (e.first.second == "MR")
			mr.add(e.first, e.second);
	if (auto r = mr.top(need))
		return r;
	if (!mr.empty())
		return std::nullopt;
	return votes.top(need);
}

json opt(const std::optional<std::string> &s)
{
	return s && !s->empty() ? json(*s) : json(nullptr);
}

json opt_str(const std::string &s)
{
	return s.empty() ? json(nullptr) : json(s);
}

std::vector<uint8_t> make_thumb(const Frame &f)
{
	// cv2.resize(gray, (48, 27), INTER_AREA)
	const int W = 48, H = 27;
	std::vector<uint8_t> out(size_t(W) * H, 0);
	if (!f.data || f.width < W || f.height < H)
		return out;
	const double sx = double(f.width) / W, sy = double(f.height) / H;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			double acc = 0, area = 0;
			double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
			for (int yy = int(y0); yy < std::min(f.height, int(std::ceil(y1))); ++yy) {
				double wy = std::min(y1, yy + 1.0) - std::max(y0, double(yy));
				for (int xx = int(x0); xx < std::min(f.width, int(std::ceil(x1))); ++xx) {
					double wx = std::min(x1, xx + 1.0) - std::max(x0, double(xx));
					const uint8_t *p = f.data + size_t(yy) * f.stride + size_t(xx) * 4;
					acc += (0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2]) * wx * wy;
					area += wx * wy;
				}
			}
			out[size_t(y) * W + x] = uint8_t(std::lround(acc / area));
		}
	return out;
}

} // namespace

// ---- FrameContext ---------------------------------------------------------------

FrameContext::FrameContext(const Frame &look, const Image &full, Ocr &ocr, const FrameContext *previous)
	: look_(look), full_(full), ocr_(ocr)
{
	thumb = make_thumb(look);
	// Same picture as the last look (menus, paused screens): reuse what was read...
	if (previous && previous->thumb.size() == thumb.size()) {
		int diff = 0;
		for (size_t i = 0; i < thumb.size(); ++i)
			diff = std::max(diff, std::abs(int(thumb[i]) - int(previous->thumb[i])));
		if (diff <= 4)
			for (const auto &e : previous->texts_)
				if (e.first != "res_p1" && e.first != "res_p2") // ...except where digits count up
					texts_.insert(e);
	}
}

const Items &FrameContext::region(const std::string &name)
{
	auto it = texts_.find(name);
	if (it != texts_.end())
		return it->second;
	const Region &r = regions().at(name);
	return texts_[name] = ocr_.read(full_, &r.roi, 0, 16, r.scale);
}

// ---- Detector --------------------------------------------------------------------

Detector::Detector(Config &cfg, Store &store, Ocr &ocr) : cfg_(cfg), store_(store), ocr_(ocr)
{
	c_ = cfg_.get();
}

std::vector<std::string> Detector::names() const
{
	std::vector<std::string> out;
	json n = jget(jget(c_, "player"), "names");
	if (n.is_array())
		for (const auto &e : n)
			if (e.is_string()) {
				std::string s = e.get<std::string>();
				if (s.find_first_not_of(" \t\r\n") != std::string::npos)
					out.push_back(s);
			}
	return out;
}

std::string Detector::wanted_label() const
{
	json v = jget(jget(c_, "player"), "rating_label");
	std::string s = v.is_string() ? v.get<std::string>() : "auto";
	std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(std::toupper((unsigned char)c)); });
	return (s == "MR" || s == "LP") ? s : "";
}

int Detector::dint(const char *key, int def) const
{
	json v = jget(jget(c_, "detect"), key);
	return v.is_number() ? int(v.get<double>()) : def;
}

Detector::Match Detector::new_match(double now)
{
	Match m;
	m.seen = now;
	return m;
}

bool Detector::add_rush(int n, double now)
{
	rush_seen_ = now;
	if (n > 0 && m_ && !active_ && now - m_->hud_seen < 40) {
		m_->rush += n;
		return true;
	}
	return false;
}

void Detector::take(Match &m, const Items &items, const char *side)
{
	const auto my_names = names();
	const std::string want = wanted_label();
	const int lo = dint("rating_min", 100), hi = dint("rating_max", 99999);
	for (const auto &it : items) {
		std::string s = side ? side : (it.cx < 0.5 ? "p1" : "p2");
		bool name_line = side ? it.cy < 0.06 : (0.74 < it.cy && it.cy < 0.83); // the line the character name is on
		if (name_line) {
			std::string cid = match_character(it.text);
			if (!cid.empty())
				m.chars[idx(s)].add(cid);
		}
		auto tk = tokens(it.text);
		if (side && tk.size() == 1 && tk[0] == "YOU") {
			m.side.add(s, 3);
			m.you++;
		} else if (!my_names.empty() && name_matches(it.text, my_names)) {
			m.side.add(s, 1);
			m.named++;
		}
		Items one{it};
		for (const auto &r : find_ratings(one, lo, hi))
			if (want.empty() || r.label == want)
				m.rating[idx(s)].add({r.value, r.label});
	}
}

void Detector::result_clues(FrameContext &ctx)
{
	// Result screen: my own rating stands next to my name, the opponent's is not shown.
	const auto my_names = names();
	const int lo = dint("rating_min", 100), hi = dint("rating_max", 99999);
	for (const char *s : {"p1", "p2"}) {
		const Items &row = ctx.region(std::string("res_") + s);
		if (!find_ratings(row, lo, hi).empty())
			res_side_.add(s, 2);
		if (!my_names.empty() &&
		    std::any_of(row.begin(), row.end(), [&](const TextItem &it) { return name_matches(it.text, my_names); }))
			res_side_.add(s, 1);
	}
	std::set<std::string> seats;
	for (const char *k : {"player_p1", "player_p2"})
		for (const auto &it : ctx.region(k)) {
			std::string seat = player_seat(it.text);
			if (!seat.empty())
				seats.insert(seat);
		}
	res_label_ = seats.size() == 1 ? *seats.begin() : "";
}

bool Detector::vs_screen(FrameContext &ctx, double now)
{
	// Is this the VS screen? Menus have nothing cheap to tell them apart, so outside a match this
	// is tried only every 2.5 s (the screen stays about 4 s).
	Items both = ctx.region("vs_p1");
	const Items &b = ctx.region("vs_p2");
	both.insert(both.end(), b.begin(), b.end());
	bool found = !find_ratings(both).empty();
	vs_next_ = found ? now : now + 2.5;
	return found;
}

std::pair<std::string, std::string> Detector::side_now() const
{
	if (m_ && m_->you)
		return {*m_->side.top(), "you"};
	if (!res_side_.empty())
		return {*res_side_.top(), "result"};
	if (m_ && !m_->side.empty())
		return {*m_->side.top(), "name"};
	if (!res_label_.empty())
		return {res_label_, "label"};
	json mine = jget(jget(c_, "player"), "chars");
	if (m_ && mine.is_array() && !mine.empty()) {
		std::vector<std::string> hit;
		for (const char *s : {"p1", "p2"}) {
			auto t = m_->chars[idx(s)].top();
			if (t && std::find(mine.begin(), mine.end(), json(*t)) != mine.end())
				hit.push_back(s);
		}
		if (hit.size() == 1)
			return {hit[0], "char"};
	}
	json side = jget(jget(c_, "player"), "side");
	return {side.is_string() ? side.get<std::string>() : "p1", "default"};
}

bool Detector::complete(const Match &m) const
{
	if (m.you < 2)
		return false;
	std::string my = *m.side.top();
	return m.chars[0].top(2) && m.chars[1].top(2) && my_rating(m.rating[idx(my)], 2);
}

void Detector::settle_previous(const std::optional<RatingKey> &r, const std::string &my_char, double now)
{
	// A rating seen after a match (start of the next one, or my card on the menu) is where that match left it.
	auto last = store_.last();
	if (!r || !last || jget(*last, "session") != json(store_.session_id()) || !jget(*last, "rating").is_null())
		return;
	json before = jget(*last, "rating_before");
	if (before.is_null() || jget(*last, "rating_label") != json(r->second) ||
	    (!my_char.empty() && jget(*last, "my_char") != json(my_char)))
		return;
	int b = before.get<int>();
	if (now - jget(*last, "ts").get<double>() < 1800 && std::abs(r->first - b) > 0 && std::abs(r->first - b) <= 1500)
		store_.update(jget(*last, "id").get<int>(), {{"rating", r->first}, {"delta", r->first - b}});
}

void Detector::arm_recovery()
{
	if (active_ || !m_ || m_->hud_seen <= 0 || (m_->you < 2 && m_->named < 2))
		return;
	auto my = m_->side.top(2);
	if (!my) return;
	auto mc = m_->chars[idx(*my)].top(2), oc = m_->chars[idx(other(*my))].top(2);
	auto r = my_rating(m_->rating[idx(*my)], 2);
	if (!mc || !oc || !r) return;
	if (recovery_) {
		// No rating change can identify the previous result. Follow the next confirmed battle instead.
		if (recovery_->candidate || recovery_->before != *r ||
		    (recovery_->fields["side"] == json(*my) && recovery_->fields["my_char"] == json(*mc) &&
		     recovery_->fields["opp_char"] == json(*oc))) return;
	}
	auto last = store_.last();
	Recovery a;
	a.before = *r;
	a.session = store_.session_id();
	a.started = m_->hud_seen;
	a.last_id = last ? jget(*last, "id") : json(nullptr);
	a.fields = {{"auto", true}, {"side", *my}, {"side_by", "rating"}, {"my_char", *mc}, {"opp_char", *oc},
		    {"rating_before", r->first}, {"rating_label", r->second}, {"note", "レート変動から補完（勝敗画面未検出）"}};
	recovery_ = std::move(a);
	m_->hud_next = std::min(m_->hud_next, m_->hud_seen + 5);
}

std::optional<json> Detector::recover_rating(const Match &probe, double now)
{
	if (!recovery_) return std::nullopt;
	auto last = store_.last();
	if (recovery_->session != store_.session_id() || now - recovery_->started > 900 ||
	    (last ? jget(*last, "id") : json(nullptr)) != recovery_->last_id) {
		recovery_.reset();
		return std::nullopt;
	}
	auto side = probe.side.top();
	auto mc = side ? probe.chars[idx(*side)].top() : std::nullopt;
	auto r = side ? my_rating(probe.rating[idx(*side)]) : std::nullopt;
	if (!side || !mc || !r || (!probe.you && !probe.named)) {
		recovery_->candidate.reset(); recovery_->confirmations = 0;
		return std::nullopt;
	}
	if (json(*mc) != recovery_->fields["my_char"] || r->second != recovery_->before.second) {
		recovery_.reset();
		return std::nullopt;
	}
	int delta = r->first - recovery_->before.first;
	if (!delta || std::abs(delta) > 1500) {
		recovery_->candidate.reset(); recovery_->confirmations = 0;
		return std::nullopt;
	}
	if (recovery_->candidate == r) ++recovery_->confirmations;
	else { recovery_->candidate = r; recovery_->confirmations = 1; }
	if (recovery_->confirmations < 3) return std::nullopt;
	json fields = recovery_->fields;
	recovery_.reset();
	if (last && jget(*last, "my_char") == fields["my_char"] && jget(*last, "rating_label") == fields["rating_label"] &&
	    jget(*last, "rating") == json(r->first)) return std::nullopt;
	fields["ts"] = now;
	fields["rating"] = r->first;
	fields["delta"] = delta;
	fields["result"] = delta > 0 ? "win" : "lose";
	last_record_ = now;
	return store_.add(fields);
}

json Detector::record(const std::string &p1res, double now)
{
	recovery_.reset(); // the result screen was caught: never add a second record from its rating change
	auto [my, by] = side_now();
	const std::string opp = other(my);
	std::string result = my == "p1" ? p1res : flip(p1res);
	json fields = {{"ts", now},   {"result", result}, {"auto", true},
		       {"side", my},  {"side_by", by},    {"rating_label", opt_str(wanted_label())}};
	std::optional<int> before;
	std::string label;
	if (m_) {
		fields["my_char"] = opt(m_->chars[idx(my)].top());
		fields["opp_char"] = opt(m_->chars[idx(opp)].top());
		auto r = my_rating(m_->rating[idx(my)], 2);
		if (!r)
			r = my_rating(m_->rating[idx(my)]);
		if (r) {
			before = r->first;
			label = r->second;
			fields["rating_before"] = r->first;
			fields["rating_label"] = r->second;
		}
		// only when the input display was counting during this match; otherwise unknown, not zero
		if (m_->hud_seen > 0 && now - rush_seen_ < 15)
			fields["rushes"] = m_->rush;
	}
	auto prev = store_.last();
	json rec = store_.add(fields);
	bool same = prev && jget(*prev, "session") == jget(rec, "session") &&
		    jget(*prev, "rating_label") == jget(rec, "rating_label") && jget(*prev, "my_char") == jget(rec, "my_char");
	Active a;
	a.id = rec["id"].get<int>();
	a.start = a.seen = now;
	a.side = my;
	a.before = before;
	a.label = label.empty() ? wanted_label() : label;
	if (same && !jget(*prev, "rating").is_null())
		a.prev = jget(*prev, "rating").get<int>();
	active_ = a;
	m_.reset();
	res_side_.clear();
	last_side_ = my;
	last_record_ = now;
	return rec;
}

void Detector::result_rating(FrameContext &ctx, double now)
{
	// Read my rating off the result screen. It counts up from the old value, so wait until it stands still.
	Active &a = *active_;
	if (a.settled || now - a.start > 45)
		return;
	std::set<RatingKey> cands;
	for (const auto &r : find_ratings(ctx.region("res_" + a.side), dint("rating_min", 100), dint("rating_max", 99999)))
		if ((a.label.empty() || r.label == a.label) && (!a.before || std::abs(r.value - *a.before) <= 1500))
			cands.insert({r.value, r.label});
	if (cands.size() > 1 && std::any_of(cands.begin(), cands.end(), [](const RatingKey &k) { return k.second == "MR"; })) {
		std::set<RatingKey> mr;
		for (const auto &k : cands)
			if (k.second == "MR")
				mr.insert(k);
		cands = mr;
	}
	if (cands.size() != 1)
		return;
	a.readings.push_back(*cands.begin());
	const auto &r = a.readings;
	auto [v, label] = r.back();
	if (r.size() < 3 || r[r.size() - 2] != r.back() || r[r.size() - 3] != r.back() || (a.before && v == *a.before))
		return; // not there yet, or still counting
	json delta = nullptr;
	if (a.before)
		delta = v - *a.before;
	else if (a.prev)
		delta = v - *a.prev;
	store_.update(a.id, {{"rating", v}, {"delta", delta}, {"rating_label", label}});
	a.settled = true;
}

std::optional<json> Detector::process(const Frame &look, const Image &full, double now)
{
	c_ = cfg_.get();
	if (recovery_session_ != store_.session_id()) {
		recovery_session_ = store_.session_id();
		recovery_.reset();
		m_.reset();
	}
	auto ctx = std::make_unique<FrameContext>(look, full, ocr_, last_ctx_.get());
	std::optional<json> event;
	if (recovery_ && (recovery_->session != store_.session_id() || now - recovery_->started > 900))
		recovery_.reset();
	bool vs = false;
	if (m_ && now - std::max(m_->seen, m_->hud_seen) > 900)
		m_.reset();
	hurry_ = 0;

	// Two colour checks decide whether anything gets read at all. During a battle nothing does,
	// apart from a look at the HUD now and then.
	bool hud_on = battle_hud_likely(look);
	Items bar = result_bar_likely(look) ? ctx->region("bar") : Items{};
	auto words = result_words(bar);
	std::string p1res = p1_result(bar);
	if (!p1res.empty()) {
		if (active_) {
			active_->seen = now;
			result_rating(*ctx, now);
			hurry_ = (!active_->settled && now - active_->start < 20) ? 0.4 : 0.0;
		} else if (!m_ && last_record_ > 0 && now - last_record_ < 600) {
			// the result screen of the match that is already recorded, back after a menu
		} else {
			result_clues(*ctx);
			streak_ = {p1res, streak_.first == p1res ? streak_.second + 1 : 1};
			// "WIN" and "LOSE" side by side in that small bar is unmistakable, and a quick rematch
			// takes the screen away within a second or two: record at once.
			if (words.size() == 2 || streak_.second >= dint("confirm_ticks", 2)) {
				event = record(p1res, now);
				result_rating(*ctx, now);
				streak_ = {"", 0};
			}
			hurry_ = 0.3;
		}
	} else {
		streak_ = {"", 0};
		res_side_.clear();
		res_label_.clear();
		if (active_ && now - active_->seen > 6)
			active_.reset();
		if (active_) {
		} else if (hud_on) {
			if (m_) {
				m_->hud_seen = now;
				// After the last K.O. a quick rematch leaves the result bar up for about a second,
				// so a match is watched closely. These looks cost next to nothing (no reading).
				hurry_ = 0.5;
			}
			bool done = m_ && (complete(*m_) || m_->hud_reads >= 10);
			if (!done || now >= m_->hud_next) {
				Match probe = new_match(now);
				for (const char *s : {"p1", "p2"})
					take(probe, ctx->region(std::string("hud_") + s), s);
				event = recover_rating(probe, now);
				std::optional<std::string> nw[2] = {probe.chars[0].top(), probe.chars[1].top()};
				if (probe.you || nw[0] || nw[1]) { // really the battle HUD
					if (m_) {
						for (int s = 0; s < 2; ++s) {
							auto old = my_rating(m_->rating[s], 2), cur = my_rating(probe.rating[s]);
							if (old && cur && cur->second == old->second && *cur != *old)
								m_->changed[s].add(*cur);
						}
						bool other_match = false;
						for (int s = 0; s < 2; ++s) {
							auto known = m_->chars[s].top(2);
							if (nw[s] && known && *known != *nw[s])
								other_match = true; // other characters than noted: already the next match
							if (m_->changed[s].top(2))
								other_match = true; // or another rating
						}
						if (other_match)
							m_.reset();
					}
					if (!m_)
						m_ = new_match(now);
					m_->seen = m_->hud_seen = now;
					m_->hud_reads++;
					m_->hud_next = now + (recovery_ ? (recovery_->candidate ? 0.5 : 5) : 30);
					for (const char *s : {"p1", "p2"})
						take(*m_, ctx->region(std::string("hud_") + s), s);
				}
			}
		} else if (m_ && now - m_->hud_seen < 40) {
			// HUD gone in the middle of a match: a cinematic, or the win pose right before the result bar
			hurry_ = 0.3;
		} else if (now >= vs_next_ && vs_screen(*ctx, now)) {
			vs = true;
			hurry_ = 0.5;
			if (!m_ || (!m_->vs_last && m_->hud_reads) || (m_->vs_last && now - *m_->vs_last > 30))
				m_ = new_match(now); // a fresh VS screen starts a new match
			m_->seen = now;
			m_->vs_last = now;
			if (m_->vs_reads < 4) {
				m_->vs_reads++;
				take(*m_, ctx->region("vs_band"), nullptr);
			}
			Match probe = new_match(now);
			take(probe, ctx->region("vs_band"), nullptr);
			event = recover_rating(probe, now);
		} else if (!m_ && now >= card_next_) {
			auto last = store_.last();
			if (last && jget(*last, "rating").is_null() && !jget(*last, "rating_before").is_null() &&
			    jget(*last, "session") == json(store_.session_id()) && now - jget(*last, "ts").get<double>() < 600) {
				// the main menu shows my own card (name, rank, rating) in the top right corner
				card_next_ = now + 3;
				Match probe = new_match(now);
				take(probe, ctx->region("hud_p2"), "p2");
				if (probe.named || names().empty())
					settle_previous(my_rating(probe.rating[1]), "", now);
			}
		}
		if (m_ && !m_->linked && complete(*m_)) {
			m_->linked = true;
			std::string my = *m_->side.top();
			auto mc = m_->chars[idx(my)].top();
			settle_previous(my_rating(m_->rating[idx(my)], 2), mc ? *mc : "", now);
		}
		arm_recovery();
	}

	std::string my, my_by;
	if (active_) {
		my = active_->side;
		my_by = "recorded";
	} else {
		std::tie(my, my_by) = side_now();
	}
	std::string mine = p1res.empty() ? "" : (my == "p1" ? p1res : flip(p1res));
	json texts = json::array();
	for (const auto &it : bar)
		texts.push_back(it.text);
	debug_ = {{"time", now},
		  {"vs", vs},
		  {"result", opt_str(mine)},
		  {"signals",
		   {{"vs", {{"texts", json::array()}}},
		    {"win", {{"texts", mine == "win" ? texts : json::array()}}},
		    {"lose", {{"texts", mine == "lose" ? texts : json::array()}}}}},
		  {"state", active_ ? "result" : (m_ ? "match" : "idle")},
		  {"side", my},
		  {"side_by", my_by},
		  {"pending", nullptr},
		  {"rating", nullptr}};
	if (m_) {
		json pend = json::object(), rat = json::object();
		for (const char *s : {"p1", "p2"}) {
			json c = json::object(), r = json::array();
			for (const auto &e : m_->chars[idx(s)].items())
				c[e.first] = e.second;
			for (const auto &e : m_->rating[idx(s)].items())
				r.push_back(std::to_string(e.first.first) + " " + e.first.second);
			pend[s] = c;
			rat[s] = r;
		}
		debug_["pending"] = pend;
		debug_["rating"] = rat;
	}
	last_ctx_ = std::move(ctx);
	return event;
}

json Detector::inspect(const Frame &look, const Image &full)
{
	c_ = cfg_.get();
	FrameContext ctx(look, full, ocr_, nullptr);
	auto keep_m = m_;
	auto keep_side = res_side_;
	auto keep_label = res_label_;
	m_ = new_match(0);
	res_side_.clear();
	res_label_.clear();
	const Items &bar = ctx.region("bar");
	std::string p1res = p1_result(bar);
	bool vs = false;
	if (p1res.empty()) {
		Items both = ctx.region("vs_p1");
		const Items &b = ctx.region("vs_p2");
		both.insert(both.end(), b.begin(), b.end());
		vs = !find_ratings(both).empty();
	}
	if (!p1res.empty())
		result_clues(ctx);
	else if (vs)
		take(*m_, ctx.region("vs_band"), nullptr);
	else
		for (const char *s : {"p1", "p2"})
			take(*m_, ctx.region(std::string("hud_") + s), s);
	auto [side, by] = side_now();
	std::string mine = p1res.empty() ? "" : (side == "p1" ? p1res : flip(p1res));
	json texts = json::array();
	for (const auto &it : bar)
		texts.push_back(it.text);
	std::optional<RatingKey> r;
	if (!p1res.empty()) {
		Counter<RatingKey> votes;
		for (const auto &f : find_ratings(ctx.region("res_" + side), dint("rating_min", 100), dint("rating_max", 99999)))
			votes.add({f.value, f.label});
		r = my_rating(votes);
	} else {
		r = my_rating(m_->rating[idx(side)]);
	}
	json out = {{"signals",
		     {{"vs", {{"found", vs}, {"texts", json::array()}}},
		      {"win", {{"found", mine == "win"}, {"texts", mine == "win" ? texts : json::array()}}},
		      {"lose", {{"found", mine == "lose"}, {"texts", mine == "lose" ? texts : json::array()}}}}},
		    {"chars", {{"p1", opt(m_->chars[0].top())}, {"p2", opt(m_->chars[1].top())}}},
		    {"side", {{"side", side}, {"by", by}}},
		    {"rating", r ? json(r->first) : json(nullptr)},
		    {"rating_label", r ? json(r->second) : json(nullptr)}};
	// Everything that was read, for the calibration screen.
	json all = json::array();
	for (const auto &e : ctx.texts_)
		for (const auto &it : e.second)
			all.push_back({{"text", it.text}, {"conf", it.conf}, {"x", it.x}, {"y", it.y}, {"w", it.w}, {"h", it.h},
				       {"cx", it.cx}, {"cy", it.cy}, {"region", e.first}});
	out["all_texts"] = all;
	m_ = keep_m;
	res_side_ = keep_side;
	res_label_ = keep_label;
	return out;
}

} // namespace sf6
