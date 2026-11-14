// A very small HTTP/1.1 server for the overlay, the control panel and the JSON API.
// Listens on 127.0.0.1 only and answers only requests addressed to this PC.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace sf6 {

struct HttpRequest {
	std::string method, path, query, body;
	std::map<std::string, std::string> headers; // lower-case names
	std::string param(const std::string &name) const;
};

struct HttpResponse {
	int code = 200;
	std::string type = "application/json; charset=utf-8";
	std::string body;
	std::map<std::string, std::string> headers;
};

class HttpServer {
public:
	using Handler = std::function<HttpResponse(const HttpRequest &)>;
	explicit HttpServer(Handler handler);
	~HttpServer();
	bool start(int port, std::string &error);
	void stop();

private:
	void accept_loop();
	void serve(intptr_t sock);

	Handler handler_;
	intptr_t listen_ = -1;
	std::atomic<bool> running_{false};
	std::atomic<int> clients_{0};
	std::thread thread_;
};

std::string url_decode(const std::string &s);

} // namespace sf6
