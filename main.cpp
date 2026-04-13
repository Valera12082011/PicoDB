#include <httplib.h>
#include <thread>
#include "include/Storage.hpp"
#include "include/Parser.hpp"

int main() {
    Storage store;
    std::thread([&store]() {
        while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); store.cleanup(); }
    }).detach();

    httplib::Server svr;
    svr.Post("/exec", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content(CommandParser::execute(store, req.body) + "\n", "text/plain");
    });

    std::cout << "PicoDB Reactive Engine started on port 8080..." << std::endl;
    svr.listen("0.0.0.0", 8080);
    return 0;
}