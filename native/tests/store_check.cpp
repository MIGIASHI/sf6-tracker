// JSON-line adapter for exercising the real native Store from the shared Python tests.
#include "../src/store.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	try {
		auto store = std::make_unique<sf6::Store>(argv[1]);
		std::cout << "{\"ready\":true}" << std::endl;
		std::string line;
		while (std::getline(std::cin, line)) {
			auto command = sf6::json::parse(line);
			std::string op = command["op"];
			sf6::json out;
			if (op == "add") out = store->add(command["fields"]);
			else if (op == "update") {
				auto rec = store->update(command["id"], command["fields"]);
				out = rec ? *rec : sf6::json(nullptr);
			} else if (op == "get") {
				auto rec = store->get(command["id"]);
				out = rec ? *rec : sf6::json(nullptr);
			} else if (op == "matches") out = store->matches();
			else if (op == "session") out = store->new_session();
			else if (op == "last") {
				auto rec = store->last();
				out = rec ? *rec : sf6::json(nullptr);
			} else if (op == "reload") {
				store = std::make_unique<sf6::Store>(argv[1]);
				out = true;
			} else throw std::runtime_error("unknown operation");
			std::cout << out.dump() << std::endl;
		}
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}
}
