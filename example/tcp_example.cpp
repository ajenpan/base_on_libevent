#include <assert.h>
#include <iostream>

#include "spdlog/spdlog.h"

#include "../TCPClient.hpp"
#include "../TCPServer.hpp"

using namespace base_on_libevent;

std::string listenAddr = ":9000";
std::string connectAddr = "127.0.0.1:9000";

auto pServer = std::make_shared<TCPServer>(8);
auto pClient = std::make_shared<TCPClient>();

int StartClient(EventLoopPtr pBase) {
    pClient->_onConnRecv = [](TCPConnPtr socket, const char* pData, uint32_t nLen) {
        // spdlog::debug("socket:{},recv:{}", socket->ID(), std::string(pData, nLen));
        auto ok = socket->Send(pData, nLen);
        if (!ok) {
            spdlog::error("client send failed");
        }
    };

    pClient->_onConnEnable = [](TCPConnPtr socket, bool enable) {
        spdlog::debug("socket:{},enable:{},read:{},write:{}", socket->ID(), enable, socket->readsize, socket->writesize);
        if (enable) {
            static std::string body = "hello workd";
            auto ok = socket->Send(body.c_str(), body.length());
            if (!ok) {
                spdlog::error("send failed");
            }
        }
    };

    auto ok = pClient->Connect("bf-panxiaof01:9000", pBase);
    spdlog::info("connect result:{}", ok);
    return 0;
}

void StartServer(EventLoopPtr pBase) {
    pServer->_onConnEnable = [](TCPConnPtr socket, bool ok) {
        //spdlog::debug("socket:{},stat:{},read:{},write:{}", socket->ID(), ok, socket->readsize, socket->writesize);
    };

    pServer->_onConnRecv = [](TCPConnPtr socket, const char* pData, uint32_t nLen) {
        auto ok = socket->Send(pData, nLen);
        if (!ok) {
            spdlog::error("send failed");
        }

        //std::shared_ptr<std::string> data = std::make_shared<std::string>(pData, nLen);
        //spdlog::debug("socket:{},recv:{}", socket->ID(), *data);
        //auto pbase = pServer->NextWorker();
        //pbase->RunInLoop([socket, data]() {
        //auto ok = socket->Send(data->c_str(), data->size());
        //if (!ok) {
        //    spdlog::error("send failed");
        //}
        //});
    };

    auto ok = pServer->Start(listenAddr, pBase);
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

    spdlog::set_level(spdlog::level::warn);

    auto pBase = std::make_shared<EventLoop>();

    if (v == 0) {
        StartServer(nullptr);
    } else if (v == 1) {
        StartClient(pBase);
    } else if (v == 2) {
        StartServer(nullptr);
        StartClient(pBase);
    }

    pBase->Run();
    pBase->Stop();
    spdlog::info("stop");
    return 0;
}