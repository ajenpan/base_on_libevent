#include "../HttpServer.hpp"

using namespace base_on_libevent;

int main(int argc, char** argv) {
    int port = 12333;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    auto pLoop = std::make_shared<EventLoopThread>();

    HttpServer svr(8, pLoop);

    std::cout << "port:" << port << std::endl;
    auto ok = svr.Start(port, [](HttpServer::Request& r, HttpServer::Response& w) {
        std::string body;
        r.ReadBodyTo(body);
        w.Reply(200, body);
    });

    if (!ok) {
        std::cout << "start failed" << std::endl;
        return -1;
    }

    std::cout << "start finished" << std::endl;
    pLoop->Run();
    return 0;
}