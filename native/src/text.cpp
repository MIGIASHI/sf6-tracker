#include "text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace sf6 {

const std::vector<Character> &roster()
{
	// Same list and order as ROSTER in sf6tracker/config.py.
	static const std::vector<Character> list = {
		{"ryu", "Ryu", "リュウ", {}},
		{"luke", "Luke", "ルーク", {}},
		{"jamie", "Jamie", "ジェイミー", {}},
		{"chunli", "Chun-Li", "春麗", {"CHUNLI"}},
		{"guile", "Guile", "ガイル", {}},
		{"kimberly", "Kimberly", "キンバリー", {}},
		{"juri", "Juri", "ジュリ", {}},
		{"ken", "Ken", "ケン", {}},
		{"blanka", "Blanka", "ブランカ", {}},
		{"dhalsim", "Dhalsim", "ダルシム", {}},
		{"ehonda", "E.Honda", "E.本田", {"EHONDA", "HONDA"}},
		{"deejay", "Dee Jay", "ディージェイ", {"DEEJAY"}},
		{"manon", "Manon", "マノン", {}},
		{"marisa", "Marisa", "マリーザ", {}},
		{"jp", "JP", "JP", {}},
		{"zangief", "Zangief", "ザンギエフ", {}},
		{"lily", "Lily", "リリー", {}},
		{"cammy", "Cammy", "キャミィ", {}},
		{"rashid", "Rashid", "ラシード", {}},
		{"aki", "A.K.I.", "A.K.I.", {"AKI"}},
		{"ed", "Ed", "エド", {}},
		{"akuma", "Akuma", "豪鬼", {"GOUKI"}},
		{"bison", "M.Bison", "ベガ", {"MBISON", "BISON", "VEGA"}},
		{"terry", "Terry", "テリー", {}},
		{"mai", "Mai", "舞", {}},
		{"elena", "Elena", "エレナ", {}},
		{"sagat", "Sagat", "サガット", {}},
		{"cviper", "C.Viper", "C.ヴァイパー", {"CVIPER", "VIPER"}},
		{"alex", "Alex", "アレックス", {}},
		{"ingrid", "Ingrid", "イングリッド", {}},
	};
	return list;
}

std::string char_name(const std::string &id, const std::string &lang)
{
	for (const auto &c : strive_roster())
		if (id == c.id)
			return lang == "ja" ? c.ja : c.en;
	if (id.empty())
		return "?";
	for (const auto &c : roster())
		if (id == c.id)
			return lang == "ja" ? c.ja : c.en;
	return id;
}

const std::vector<Character> &strive_roster()
{
	// https://www.guiltygear.com/ggst/en/character/
	static const std::vector<Character> characters = {
		{"ggst_sol", "Sol Badguy", "ソル", {}},
		{"ggst_ky", "Ky Kiske", "カイ", {}},
		{"ggst_may", "May", "メイ", {}},
		{"ggst_axl", "Axl Low", "アクセル", {}},
		{"ggst_chipp", "Chipp Zanuff", "チップ", {}},
		{"ggst_potemkin", "Potemkin", "ポチョムキン", {}},
		{"ggst_faust", "Faust", "ファウスト", {}},
		{"ggst_millia", "Millia Rage", "ミリア", {}},
		{"ggst_zato", "Zato-1", "ザトー", {}},
		{"ggst_ramlethal", "Ramlethal Valentine", "ラムレザル", {}},
		{"ggst_leo", "Leo Whitefang", "レオ", {}},
		{"ggst_nagoriyuki", "Nagoriyuki", "名残雪", {}},
		{"ggst_giovanna", "Giovanna", "ジオヴァーナ", {}},
		{"ggst_anji", "Anji Mito", "闇慈", {}},
		{"ggst_ino", "I-No", "イノ", {}},
		{"ggst_goldlewis", "Goldlewis Dickinson", "ゴールドルイス", {}},
		{"ggst_jacko", "Jack-O'", "ジャック・オー", {}},
		{"ggst_happychaos", "Happy Chaos", "ハッピーケイオス", {}},
		{"ggst_baiken", "Baiken", "梅喧", {}},
		{"ggst_testament", "Testament", "テスタメント", {}},
		{"ggst_bridget", "Bridget", "ブリジット", {}},
		{"ggst_sin", "Sin Kiske", "シン", {}},
		{"ggst_bedman", "Bedman?", "ベッドマン？", {}},
		{"ggst_asuka", "Asuka R♯", "飛鳥＝R♯", {}},
		{"ggst_johnny", "Johnny", "ジョニー", {}},
		{"ggst_elphelt", "Elphelt Valentine", "エルフェルト", {}},
		{"ggst_aba", "A.B.A", "アバ", {}},
		{"ggst_slayer", "Slayer", "スレイヤー", {}},
		{"ggst_dizzy", "Queen Dizzy", "ディズィー", {}},
		{"ggst_venom", "Venom", "ヴェノム", {}},
		{"ggst_unika", "Unika", "ユニカ", {}},
		{"ggst_lucy", "Lucy", "ルーシー", {}},
		{"ggst_jam", "Jam Kuradoberi", "紗夢", {}},
		{"ggst_roboky", "Robo-Ky", "ロボカイ", {}},
	};
	return characters;
}

std::string strive_character(const std::string &text)
{
	auto compact = [](const std::string &value) {
		std::string joined;
		for (const auto &token : tokens(value))
			joined += token;
		return joined;
	};
	const auto name = compact(text);
	if (name.size() < 3)
		return {};
	for (const auto &character : strive_roster()) {
		const auto English = compact(character.en);
		if (name == English || (name.size() >= 8 &&
			sequence_ratio(utf8_to_u32(name), utf8_to_u32(English)) >= 0.88))
			return character.id;
	}
	return {};
}

namespace {

bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}
bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}
bool is_ascii_alnum(char c)
{
	return is_digit(c) || is_upper(c) || (c >= 'a' && c <= 'z');
}

std::string upper(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
		if (c >= 'a' && c <= 'z')
			c = char(c - 32);
	return out;
}

// Length of the white space (ASCII or the ideographic space) starting at i.
size_t spaces(const std::string &t, size_t i)
{
	size_t j = i;
	for (;;) {
		if (j < t.size() && (t[j] == ' ' || (t[j] >= '\t' && t[j] <= '\r')))
			++j;
		else if (t.compare(j, 3, "\xE3\x80\x80") == 0)
			j += 3;
		else
			return j - i;
	}
}

size_t digit_run(const std::string &t, size_t i)
{
	size_t j = i;
	while (j < t.size() && is_digit(t[j]))
		++j;
	return j - i;
}

// Ends of the number group (\d{1,3}(?:,\d{3})+|\d{3,6}) starting at i, in the order the regex tries them.
std::vector<size_t> number_ends(const std::string &t, size_t i)
{
	std::vector<size_t> ends;
	size_t run = digit_run(t, i);
	for (size_t lead = std::min<size_t>(3, run); lead >= 1; --lead) {
		std::vector<size_t> groups;
		size_t j = i + lead;
		while (j + 4 <= t.size() && t[j] == ',' && digit_run(t, j + 1) >= 3) {
			j += 4;
			groups.push_back(j);
		}
		for (auto it = groups.rbegin(); it != groups.rend(); ++it)
			ends.push_back(*it);
	}
	for (size_t n = std::min<size_t>(6, run); n >= 3; --n)
		ends.push_back(i + n);
	return ends;
}

int number_value(const std::string &t, size_t a, size_t b)
{
	long v = 0;
	for (size_t i = a; i < b; ++i)
		if (is_digit(t[i]))
			v = v * 10 + (t[i] - '0');
	return int(std::min<long>(v, 99999999));
}

bool label_at(const std::string &t, size_t i)
{
	return t.compare(i, 2, "LP") == 0 || t.compare(i, 2, "MR") == 0;
}

// str.isalnum() for one code point, close enough for player names (letters, digits, kana, kanji).
bool cp_alnum(char32_t c)
{
	if (c < 0x80)
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	return !((c >= 0x80 && c <= 0xBF) || c == 0xD7 || c == 0xF7 || (c >= 0x2000 && c <= 0x2BFF) ||
		 (c >= 0x3000 && c <= 0x3004) || (c >= 0x3008 && c <= 0x3020) || c == 0x3030 || c == 0x30FB ||
		 (c >= 0xE000 && c <= 0xF8FF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF01 && c <= 0xFF0F) ||
		 (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65) ||
		 (c >= 0x1F000 && c <= 0x1FAFF));
}

// Is the character that ends right before byte p a word character (regex \w)?
bool word_before(const std::string &t, size_t p)
{
	if (!p)
		return false;
	size_t s = p - 1;
	while (s > 0 && ((unsigned char)t[s] & 0xC0) == 0x80)
		--s;
	std::u32string cp = utf8_to_u32(t.substr(s, p - s));
	return !cp.empty() && (cp[0] == '_' || cp_alnum(cp[0]));
}

std::u32string fold(const std::string &text)
{
	std::u32string out;
	for (char32_t c : utf8_to_u32(text)) {
		if (c >= 'A' && c <= 'Z')
			c += 32;
		else if (c >= 0xFF21 && c <= 0xFF3A) // fullwidth A-Z
			c += 32;
		else if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9) || (c >= 0x410 && c <= 0x42F))
			c += 32;
		else if (c >= 0x400 && c <= 0x40F)
			c += 80;
		bool alnum = cp_alnum(c);
		if (alnum)
			out.push_back(c);
	}
	return out;
}

std::string norm(const std::string &text)
{
	std::string out;
	for (char c : upper(text))
		if (is_digit(c) || is_upper(c))
			out.push_back(c);
	return out;
}

struct Alias {
	std::string alias, id;
};
const std::vector<Alias> &aliases()
{
	static const std::vector<Alias> list = [] {
		std::vector<Alias> v;
		for (const auto &c : roster()) {
			v.push_back({norm(c.en), c.id});
			for (const char *e : c.extra)
				v.push_back({norm(e), c.id});
		}
		return v;
	}();
	return list;
}

} // namespace

std::u32string utf8_to_u32(const std::string &s)
{
	std::u32string out;
	for (size_t i = 0; i < s.size();) {
		unsigned char c = (unsigned char)s[i];
		char32_t cp;
		int n;
		if (c < 0x80) {
			cp = c;
			n = 1;
		} else if ((c >> 5) == 6) {
			cp = c & 0x1F;
			n = 2;
		} else if ((c >> 4) == 14) {
			cp = c & 0x0F;
			n = 3;
		} else if ((c >> 3) == 30) {
			cp = c & 0x07;
			n = 4;
		} else {
			++i;
			continue;
		}
		if (i + n > s.size())
			break;
		for (int k = 1; k < n; ++k)
			cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
		out.push_back(cp);
		i += size_t(n);
	}
	return out;
}

std::vector<std::string> tokens(const std::string &text)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : upper(text)) {
		if (is_ascii_alnum(c)) {
			cur.push_back(c);
		} else if (!cur.empty()) {
			out.push_back(cur);
			cur.clear();
		}
	}
	if (!cur.empty())
		out.push_back(cur);
	return out;
}

double sequence_ratio(const std::u32string &a, const std::u32string &b)
{
	if (a.empty() && b.empty())
		return 1.0;
	// difflib's matching blocks: the longest common run (earliest in a, then in b), then recurse
	// on both sides of it.
	size_t matched = 0;
	std::vector<std::array<size_t, 4>> queue{{0, a.size(), 0, b.size()}};
	while (!queue.empty()) {
		auto [alo, ahi, blo, bhi] = queue.back();
		queue.pop_back();
		size_t besti = alo, bestj = blo, bestk = 0;
		std::vector<size_t> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
		for (size_t i = alo; i < ahi; ++i) {
			std::fill(cur.begin(), cur.end(), 0);
			for (size_t j = blo; j < bhi; ++j) {
				if (a[i] != b[j])
					continue;
				size_t k = (j > blo ? prev[j - 1] : 0) + 1;
				cur[j] = k;
				if (k > bestk) {
					besti = i + 1 - k;
					bestj = j + 1 - k;
					bestk = k;
				}
			}
			std::swap(prev, cur);
		}
		if (!bestk)
			continue;
		matched += bestk;
		if (alo < besti && blo < bestj)
			queue.push_back({alo, besti, blo, bestj});
		if (besti + bestk < ahi && bestj + bestk < bhi)
			queue.push_back({besti + bestk, ahi, bestj + bestk, bhi});
	}
	return 2.0 * double(matched) / double(a.size() + b.size());
}

std::string match_character(const std::string &text)
{
	for (const auto &c : roster()) {
		bool ascii = true;
		for (const char *p = c.ja; *p; ++p)
			ascii = ascii && (unsigned char)*p < 0x80;
		if (!ascii && text.find(c.ja) != std::string::npos)
			return c.id;
	}
	std::string n = norm(text);
	if (n.empty())
		return "";
	for (const auto &a : aliases())
		if (n == a.alias)
			return a.id;
	// SF6 draws the control-type icon (C / M / D) right before the name; the reading glues it on.
	if (n.size() >= 3 && std::strchr("CMDG", n[0]))
		for (const auto &a : aliases())
			if (n.compare(1, std::string::npos, a.alias) == 0)
				return a.id;
	if (n.size() >= 5) { // short names (ED, JP, KEN...) must match exactly
		std::string best;
		double score = 0;
		std::u32string un = utf8_to_u32(n);
		for (const auto &a : aliases()) {
			if (a.alias.size() < 5)
				continue;
			double s = sequence_ratio(un, utf8_to_u32(a.alias));
			if (s > score) {
				best = a.id;
				score = s;
			}
		}
		if (score >= 0.8)
			return best;
	}
	return "";
}

std::vector<Rating> find_ratings(const Items &items, int lo, int hi)
{
	std::vector<Rating> out;
	for (const auto &it : items) {
		const std::string t = upper(it.text);
		size_t i = 0;
		while (i < t.size()) {
			size_t match_end = 0;
			int value = 0;
			std::string label;
			char before = i ? t[i - 1] : '\0';
			// number, then label: "18683LP", "1502 MR"
			if (is_digit(t[i]) && !(i && (is_digit(before) || std::strchr("+-.,:", before)))) {
				for (size_t e : number_ends(t, i)) {
					size_t j = e + spaces(t, e);
					if (label_at(t, j) && !(j + 2 < t.size() && is_upper(t[j + 2]))) {
						match_end = j + 2;
						value = number_value(t, i, e);
						label = t.substr(j, 2);
						break;
					}
				}
			}
			// label, then number: "MR 1620", "LP：19760"
			if (!match_end && label_at(t, i) && !(i && is_upper(before))) {
				size_t j = i + 2 + spaces(t, i + 2);
				std::vector<size_t> starts;
				if (t.compare(j, 1, ":") == 0)
					starts.push_back(j + 1);
				else if (t.compare(j, 3, "\xEF\xBC\x9A") == 0)
					starts.push_back(j + 3);
				starts.push_back(j);
				for (size_t s : starts) {
					size_t k = s + spaces(t, s);
					for (size_t e : number_ends(t, k)) {
						if (e < t.size() && is_digit(t[e]))
							continue;
						match_end = e;
						value = number_value(t, k, e);
						label = t.substr(i, 2);
						break;
					}
					if (match_end)
						break;
				}
			}
			if (match_end) {
				if (lo <= value && value <= hi)
					out.push_back({value, label, &it});
				i = match_end;
			} else {
				++i;
			}
		}
	}
	return out;
}

std::optional<int> find_rating(const Items &items, const std::string &label_in, int lo, int hi)
{
	const std::string label = upper(label_in);
	std::vector<std::pair<int, const TextItem *>> nums;
	std::vector<const TextItem *> labels;
	for (const auto &it : items) {
		bool numeric = !it.text.empty();
		for (char c : it.text)
			numeric = numeric && (is_digit(c) || c == 'O' || c == 'o' || c == ',' || c == '.' || c == ' ' ||
					      (c >= '\t' && c <= '\r'));
		std::string t = upper(it.text);
		if (numeric)
			std::replace(t.begin(), t.end(), 'O', '0');
		if (!label.empty()) {
			auto tk = tokens(t);
			bool hit = std::find(tk.begin(), tk.end(), label) != tk.end();
			// \bLABEL\s*\d
			for (size_t p = t.find(label); !hit && p != std::string::npos; p = t.find(label, p + 1)) {
				bool boundary = !word_before(t, p);
				size_t q = p + label.size();
				q += spaces(t, q);
				hit = boundary && q < t.size() && is_digit(t[q]);
			}
			if (hit)
				labels.push_back(&it);
		}
		// (?<![\d+\-])\d[\d,]{2,6}(?!\d)
		size_t i = 0;
		while (i < t.size()) {
			bool ok = is_digit(t[i]) && !(i && (is_digit(t[i - 1]) || t[i - 1] == '+' || t[i - 1] == '-'));
			size_t end = 0;
			if (ok) {
				size_t run = 0;
				while (run < 6 && i + 1 + run < t.size() && (is_digit(t[i + 1 + run]) || t[i + 1 + run] == ','))
					++run;
				for (size_t k = run; k >= 2; --k) {
					size_t e = i + 1 + k;
					if (!(e < t.size() && is_digit(t[e]))) {
						end = e;
						break;
					}
				}
			}
			if (end) {
				int v = number_value(t, i, end);
				if (lo <= v && v <= hi)
					nums.push_back({v, &it});
				i = end;
			} else {
				++i;
			}
		}
	}
	if (nums.empty())
		return std::nullopt;
	if (!labels.empty()) {
		auto dist = [&](const std::pair<int, const TextItem *> &n) {
			double best = 1e9;
			for (const TextItem *l : labels)
				best = std::min(best, std::abs(n.second->cx - l->cx) + 2 * std::abs(n.second->cy - l->cy));
			return best;
		};
		std::stable_sort(nums.begin(), nums.end(), [&](const auto &a, const auto &b) { return dist(a) < dist(b); });
		if (dist(nums[0]) < 0.25)
			return nums[0].first;
		return std::nullopt;
	}
	if (!label.empty())
		return std::nullopt;
	for (const auto &n : nums)
		if (n.first != nums[0].first)
			return std::nullopt;
	return nums[0].first;
}

std::map<std::string, double> result_words(const Items &items)
{
	std::map<std::string, double> pos;
	for (const auto &it : items) {
		std::u32string t = utf8_to_u32(upper(it.text));
		if (t.empty())
			continue;
		for (size_t i = 0; i < t.size();) {
			const char *word = nullptr;
			if (t.compare(i, 3, U"WIN") == 0)
				word = "WIN";
			else if (t.compare(i, 4, U"LOSE") == 0)
				word = "LOSE";
			if (!word) {
				++i;
				continue;
			}
			size_t n = std::strlen(word);
			if (!pos.count(word))
				pos[word] = it.x + it.w * (double(i) + double(i + n)) / 2.0 / double(t.size());
			i += n;
		}
	}
	return pos;
}

std::string p1_result(const Items &items)
{
	auto pos = result_words(items);
	bool has_w = pos.count("WIN"), has_l = pos.count("LOSE");
	if (has_w && has_l)
		return pos["WIN"] < pos["LOSE"] ? "win" : "lose";
	if (!has_w && !has_l)
		return "";
	double x = has_w ? pos["WIN"] : pos["LOSE"];
	double off = std::abs(x - 0.5);
	if (!(0.02 <= off && off <= 0.09)) // one word alone must sit where the bar puts it
		return "";
	return ((x < 0.5) == has_w) ? "win" : "lose";
}

std::string player_seat(const std::string &text)
{
	std::string t = upper(text);
	for (size_t p = t.find("PLAYER"); p != std::string::npos; p = t.find("PLAYER", p + 1)) {
		size_t q = p + 6;
		q += spaces(t, q);
		if (q < t.size() && (t[q] == '1' || t[q] == '2'))
			return std::string("p") + t[q];
	}
	return "";
}

bool name_matches(const std::string &text, const std::vector<std::string> &names)
{
	std::u32string t = fold(text);
	if (t.empty())
		return false;
	for (const auto &name : names) {
		std::u32string n = fold(name);
		if (n.empty())
			continue;
		if (t == n || (n.size() >= 3 && t.find(n) != std::u32string::npos))
			return true;
		if (n.size() >= 4 && (t.size() > n.size() ? t.size() - n.size() : n.size() - t.size()) <= 2 &&
		    sequence_ratio(t, n) >= 0.75)
			return true;
	}
	return false;
}

} // namespace sf6
