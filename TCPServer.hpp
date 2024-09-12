#pragma once

#include <atomic>
#include <map>
#include <memory>

#include <event2/listener.h>

#include "TCPConn.hpp"

namespace base_on_libevent {

class TCPServer : public CallbackBase {
  public:
    using AutoLock = std::lock_guard<std::mutex>;

    TCPServer(uint16_t workercnt = 0)
        : _threads(workercnt) {
        _threads.Start();
    }

    virtual ~TCPServer() {
        _threads.Stop();

        if (_bCreateBase) {
            if (_pBase) {
                event_base_loopbreak(_pBase->event_base());
            }
        }
    }

    int Start(std::string strAddr, EventLoopPtr pBase = nullptr) {
        // auto t = Helper::SpilitHostPort(strAddr);
        // auto strIP = std::get<0>(t);
        // auto nPort = std::get<1>(t);

        auto [strIP, nPort] = Helper::SpilitHostPort(strAddr);
        _listenAddr.sin_family = AF_INET;
        _listenAddr.sin_port = htons(nPort);
        _listenAddr.sin_addr.s_addr = strIP.empty() ? 0 : inet_addr(strIP.c_str());

        _pBase = pBase;
        _bCreateBase = _pBase == nullptr;

        if (_bCreateBase) {
            auto pTemp = std::make_shared<EventLoopThread>();
            pTemp->Start();
            _pBase = pTemp;
        }

        _listener = ::evconnlistener_new_bind(_pBase->event_base(), &TCPServer::connlistener_cb, this, LEV_OPT_CLOSE_ON_FREE, -1, (sockaddr*)&_listenAddr, sizeof(_listenAddr));

        if (!_listener) {
            SocketError("could not create a listener!");
            return -1;
        }

        // get real listen address
        socklen_t addr_len = sizeof(_listenAddr);
        getsockname(evconnlistener_get_fd(_listener), (struct sockaddr*)&_listenAddr, &addr_len);

        _bRun.store(true);
        return ntohs(_listenAddr.sin_port);
    }

    void Stop() {
        if (_bRun.exchange(false) == false) {
            return;
        }

        _pBase->RunInLoop([this]() {
            if (this->_listener) {
                evconnlistener_free(this->_listener);
                this->_listener = nullptr;
            }
            if (_bCreateBase) {
                _pBase->Stop();
            }
        });
    }

    TCPConnPtr GetConn(TCPSocketID id) {
        std::lock_guard<std::mutex> lg(_kConnLock);
        auto it = _mapConns.find(id);
        if (it == _mapConns.end()) {
            return nullptr;
        }
        return it->second;
    }

    void RemoveConn(TCPSocketID id) {
        std::lock_guard<std::mutex> lg(_kConnLock);
        auto it = _mapConns.find(id);
        if (it != _mapConns.end()) {
            auto& conn = it->second;
            _mapConns.erase(it);
        }
    }

    size_t ConnCount() {
        std::lock_guard<std::mutex> lg(_kConnLock);
        return _mapConns.size();
    }

    EventLoopPtr NextWorker() {
        if (_threads.Size() == 0) {
            return _pBase;
        }
        return _threads.GetNextLoop();
    }

  protected:
    void OnError(const std::string& msg) {
        std::cout << msg << "\n";
    }

    void SocketError(const std::string& msg) {
        int errcode = EVUTIL_SOCKET_ERROR();
        auto errstr = evutil_socket_error_to_string(errcode);
        std::cout << msg << ": " << errcode << ":" << errstr << "\n";
    }

    void AddConn(TCPSocketID id, TCPConnPtr pConn) {
        std::lock_guard<std::mutex> lg(_kConnLock);
        auto kInsert = _mapConns.insert(std::make_pair(id, pConn));
        if (!kInsert.second) {
            OnError("AddConn failed!");
        }
    }

    static void connlistener_cb(struct evconnlistener* listener, evutil_socket_t fd, struct sockaddr* sa, int socklen, void* ud) {
        TCPServer* pSvr = static_cast<TCPServer*>(ud);

        if (pSvr->_onAccept) {
            bool accept = pSvr->_onAccept(fd, sa, socklen);

            if (!accept) {
                evutil_closesocket(fd);
                return;
            }
        }

        auto id = Helper::GenSocketID();
        auto evbase = pSvr->NextWorker();

        const static int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        auto evbuff = bufferevent_socket_new(evbase->event_base(), fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
        if (evbuff == nullptr) {
            pSvr->OnError("bufferevent_socket_new");
            return;
        }

        auto pClient = std::make_shared<TCPConn>(evbase, evbuff);

        pClient->_id = id;
        pClient->_evbuff = evbuff;
        pClient->_evbase = evbase;
        pClient->_onFliterStream = pSvr->_onFliterStream;
        pClient->_onConnRecv = pSvr->_onConnRecv;
        pClient->_onConnEnable = [pSvr](TCPConnPtr conn, bool enable) {
            if (pSvr->_onConnEnable) {
                pSvr->_onConnEnable(conn, enable);
            }

            if (!enable) {

                if (conn->_evbuff) {
                    auto fd = bufferevent_getfd(conn->_evbuff);
                    evutil_closesocket(fd);

                    bufferevent_free(conn->_evbuff);
                    conn->_evbuff = nullptr;
                }

                pSvr->RemoveConn(conn->ID());
            }
        };

        pSvr->AddConn(id, pClient);

        evbase->RunInLoop([pSvr, fd, pClient]() {
            bufferevent_enable(pClient->_evbuff, EV_READ | EV_WRITE);

            pClient->_enable.store(true);
            pClient->OnConnEnable();
        });
    }

    EventLoopPtr _pBase = nullptr;
    sockaddr_in _listenAddr;
    evconnlistener* _listener = nullptr;

    EventLoopThreadPool _threads;

    std::atomic_bool _bRun;
    bool _bCreateBase = true;

    std::mutex _kConnLock;
    std::map<TCPSocketID, TCPConnPtr> _mapConns;
};
}; // namespace base_on_libevent
