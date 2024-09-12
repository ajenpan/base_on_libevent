#pragma once

#include <thread>
#include <unordered_map>
#include <functional>
#include <future>
#include <atomic>

extern "C" {
#include <event2/http.h>
#include <event2/ws.h>
}

#include "EventLoop.hpp"
#include "TCPConn.hpp"

namespace base_on_libevent {

struct WSConn : public std::enable_shared_from_this<WSConn> {
    int64_t ID() {
        return (int64_t)(imp);
    }

    struct evws_connection* imp = nullptr;
    struct sockaddr_storage addr;
    std::string rcache;

    size_t readsize = 0;
    size_t writesize = 0;

    bool Send(const char* data, size_t len) {
        evws_send(imp, data, len);
        writesize += len;
        return true;
    }

    void Close() {
        // TODO:
        //evws_close
    }
};

using WSConnPtr = std::shared_ptr<WSConn>;

class WSServer {
  public:
    typedef std::function<bool(struct evhttp_request* req)> funcOnAccept;
    typedef std::function<void(WSConnPtr socket, bool enable)> funcOnConnEnable;
    typedef std::function<void(WSConnPtr socket, const char* data, size_t len)> funcOnDataRecv;
    typedef std::function<int32_t(WSConnPtr socket, const char* data, size_t len)> funcFliterStream;

    funcOnAccept onAccept = nullptr;
    funcOnConnEnable onConnEnable = nullptr;
    funcFliterStream onFliterStream = nullptr;
    funcOnDataRecv onConnRecv = nullptr;

    WSServer(EventLoopPtr loop = nullptr) {
        _run_status = 0;
        _loop = loop;
        _createEVBaseLocal = _loop == nullptr;

        if (_createEVBaseLocal) {
            auto pLoop = std::make_shared<EventLoopThread>();
            pLoop->Start();
            _loop = pLoop;
        }
    }

    virtual ~WSServer() {
        Stop();
    }

    void Stop() {
        auto status = _run_status.exchange(0);
        if (status != 1) {
            return;
        }
        if (_createEVBaseLocal && _loop != nullptr) {
            _loop->Stop();
            _loop = nullptr;
        }
    }

    int Start(uint32_t port) {
        auto status = _run_status.exchange(1);
        if (status != 0) {
            return -1;
        }

        const char* addr = "0.0.0.0";

        auto pEvHttp = evhttp_new(_loop->event_base());
        if (pEvHttp == nullptr) {
            return -1;
        }
        evhttp_set_gencb(pEvHttp, on_handle, this);

        _evhttp = std::shared_ptr<evhttp>(pEvHttp, [](evhttp* p) {
            evhttp_free(p);
        });

        auto handle = evhttp_bind_socket_with_handle(pEvHttp, addr, port);
        if (handle == nullptr) {
            return -1;
        }
        return 0;
    }

  private:
    static void on_handle(struct evhttp_request* req, void* ud) {
        WSServer* pServer = (WSServer*)ud;

        if (pServer->onAccept) {
            auto need = pServer->onAccept(req);
            if (!need) {
                evhttp_send_reply(req, HTTP_FORBIDDEN, NULL, NULL);
                return;
            }
        }

        auto pConn = std::make_shared<WSConn>();
        auto conn = evws_new_session(req, WSServer::on_msg_cb, pServer, BEV_OPT_THREADSAFE);
        pConn->imp = conn;

        evws_connection_set_closecb(conn, on_close_cb, pServer);

        auto fd = bufferevent_getfd(evws_connection_get_bufferevent(conn));
        socklen_t len = sizeof(pConn->addr);
        getpeername(fd, (struct sockaddr*)&(pConn->addr), &len);

        pServer->conns.emplace(conn, pConn);

        if (pServer->onConnEnable) {
            pServer->onConnEnable(pConn, true);
        }
    }

    static void on_msg_cb(struct evws_connection* c, int type, const unsigned char* indata, size_t inlen, void* ud) {
        WSServer* pServer = (WSServer*)ud;

        auto kFind = pServer->conns.find(c);
        if (kFind == pServer->conns.end()) {
            return;
        }
        WSConnPtr pConn = kFind->second;

        pConn->rcache.append((const char*)indata, inlen);

        const char* buf = pConn->rcache.c_str();
        size_t len = pConn->rcache.length();

        size_t pklen = 0;
        size_t used = 0;

        while (len > 0) {
            if (pServer->onFliterStream) {
                pklen = pServer->onFliterStream(pConn, buf, len);
            } else {
                pklen = len;
            }

            if (pklen < 0 || pklen > len) {
                // 非法数据包
                pConn->Close();
                return;
            } else if (pklen == 0) {
                // 数据不够封包,继续等待
                break;
            }

            if (pServer->onConnRecv) {
                pServer->onConnRecv(pConn, buf, pklen);
            }

            used += pklen;
            len -= pklen;
            buf += pklen;
        }

        if (used > 0) {
            pConn->rcache.erase(0, used);
            pConn->readsize += used;
        }
    }

    static void on_close_cb(struct evws_connection* ws, void* ud) {
        WSServer* pServer = (WSServer*)ud;
        auto conn = pServer->GetConn(ws);
        if (conn && pServer->onConnEnable) {
            pServer->onConnEnable(conn, false);
        }

        pServer->conns.erase(ws);
    }

    WSConnPtr GetConn(evws_connection* ws) {
        auto kFind = conns.find(ws);
        if (kFind == conns.end()) {
            return nullptr;
        }
        return kFind->second;
    }
    std::map<evws_connection*, WSConnPtr> conns;

    EventLoopPtr _loop = nullptr;

    std::atomic<uint8_t> _run_status;

    bool _createEVBaseLocal = false;
    std::shared_ptr<evhttp> _evhttp = nullptr;
};
} // namespace base_on_libevent