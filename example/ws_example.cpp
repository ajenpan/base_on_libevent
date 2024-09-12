#include <assert.h>
#include <iostream>

#include "spdlog/spdlog.h"

#include "../WSServer.hpp"

using namespace base_on_libevent;

std::string listenAddr = "9000";
std::string connectAddr = "127.0.0.1:" + listenAddr;

auto pBase = std::make_shared<EventLoop>();
auto pServer = std::make_shared<WSServer>();

EventLoopThreadPool pool(8);

void StartServer() {
    pool.Start();

    pServer->onConnEnable = [](WSConnPtr conn, bool ok) {
        spdlog::debug("conn:{},stat:{},read:{},write:{}", conn->ID(), ok, conn->readsize, conn->writesize);
    };

    pServer->onConnRecv = [](WSConnPtr conn, const char* pData, uint32_t nLen) {
        auto pStrData = std::make_shared<std::string>(pData, nLen);
        spdlog::debug("conn:{},read:{}", conn->ID(), *pStrData);
        for (int i = 0; i < 10; i++) {
            auto loop = pool.GetNextLoop();
            loop->RunInLoop([conn, pStrData]() {
                conn->Send(pStrData->c_str(), pStrData->size());
            });
        }
    };

    auto ok = pServer->Start(9000);
    spdlog::info("start result:{}", ok);
}

int main(int argc, char** argv) {
    int test = 0;
    if (test) {
        std::cout << test << " is true!" << std::endl;
    }
    auto v = 0;
    if (argc > 1) {
        v = atoi(argv[1]);
    }

    spdlog::set_level(spdlog::level::debug);

    StartServer();

    pBase->Run();
    pBase->Stop();
    spdlog::info("stop");
    return 0;
}