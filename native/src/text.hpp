// Making sense of the text that was read: character names, ratings, the result bar, player names.
// C++ ports of the text helpers in sf6tracker/vision.py, written without regex look-behind
// (std::regex has none) but deciding the same way.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ocr.hpp"

namespace sf6 {

struct Character {
	const char *id, *en, *ja;
	std::vector<const char *> extra; // other spellings the reading may return
};
const std::vector<Character> &roster();
const std::vector<Character> &strive_roster();
std::string strive_character(const std::string &text);
std::string char_name(const std::string &id, const std::string &lang); // "?" for none

std::vector<std::string> tokens(const std::string &text); // upper-cased runs of A-Z / 0-9

// Character id for a text, or "" when none.
std::string match_character(const std::string &text);

struct Rating {
	int value;
	std::string label; // "MR" / "LP"
	const TextItem *item;
};
// Every "number + LP/MR" (either order, glued or spaced) in the items.
std::vector<Rating> find_ratings(const Items &items, int lo = 0, int hi = 999999);
// The rating number next to `label` ("" = any number in range, when the items come from a tight place).
std::optional<int> find_rating(const Items &items, const std::string &label, int lo, int hi);

// Where "WIN" and "LOSE" stand in the result bar (x as a fraction of the picture).
std::map<std::string, double> result_words(const Items &items);
// "win" / "lose" for player 1 from the result bar, or "".
std::string p1_result(const Items &items);
// "p1" / "p2" from "PLAYER1" / "PLAYER2", or "".
std::string player_seat(const std::string &text);

bool name_matches(const std::string &text, const std::vector<std::string> &names);

// difflib.SequenceMatcher(None, a, b).ratio() over Unicode code points.
double sequence_ratio(const std::u32string &a, const std::u32string &b);
std::u32string utf8_to_u32(const std::string &s);

} // namespace sf6
