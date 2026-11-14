// Reads JSON cases on stdin, one per line, and prints the C++ answers as JSON lines.
// {"fn": "match_character", "text": "..."} / {"fn": "name_matches", "text": "...", "names": [...]}
// {"fn": "find_ratings" | "find_rating" | "p1_result", "items": [...], "label": "", "lo": 0, "hi": 99999}
// {"fn": "player_seat" | "tokens", "text": "..."}
#include "../src/text.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;

static sf6::Items items_of(const json &j)
{
	sf6::Items out;
	for (const auto &e : j) {
		sf6::TextItem it;
		it.text = e.value("text", "");
		it.x = e.value("x", 0.0);
		it.y = e.value("y", 0.0);
		it.w = e.value("w", 0.0);
		it.h = e.value("h", 0.0);
		it.cx = e.value("cx", it.x + it.w / 2);
		it.cy = e.value("cy", it.y + it.h / 2);
		out.push_back(it);
	}
	return out;
}

int main()
{
	std::string line;
	while (std::getline(std::cin, line)) {
		json c = json::parse(line);
		std::string fn = c["fn"];
		json r;
		if (fn == "match_character") {
			r = sf6::match_character(c["text"]);
		} else if (fn == "name_matches") {
			r = sf6::name_matches(c["text"], c["names"].get<std::vector<std::string>>());
		} else if (fn == "player_seat") {
			r = sf6::player_seat(c["text"]);
		} else if (fn == "tokens") {
			r = sf6::tokens(c["text"]);
		} else {
			sf6::Items items = items_of(c["items"]);
			if (fn == "find_ratings") {
				r = json::array();
				for (const auto &f : sf6::find_ratings(items, c.value("lo", 0), c.value("hi", 999999)))
					r.push_back({f.value, f.label});
			} else if (fn == "find_rating") {
				auto v = sf6::find_rating(items, c.value("label", ""), c.value("lo", 0), c.value("hi", 999999));
				r = v ? json(*v) : json(nullptr);
			} else if (fn == "p1_result") {
				r = sf6::p1_result(items);
			}
		}
		std::cout << r.dump() << "\n";
	}
	return 0;
}
