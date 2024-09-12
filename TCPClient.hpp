#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <event2/dns.h>

#include "TCPConn.hpp"

namespace base_on_libevent {

class TCPClient : public CallbackBase {
  public:
    using Mutex = std::mutex;
    using AutoLock = std::lock_guard<Mutex>;

    TCPClient() {
    }

    virtual ~TCPClient() {
        if (_evbuff) {
            bufferevent_free(_evbuff);
            _evbuff = nullptr;
        }

        if (_pReConnEvent) {
            event_free(_pReConnEvent);
            _pReConnEvent = nullptr;
        }

        if (_bNeedBase) {
            if (_evbase) {
                _evbase->Stop();
                _evbase = nullptr;
            }
        }

        if (_addr) {
            evutil_freeaddrinfo(_addr);
            _addr = nullptr;
        }
    }

    virtual void Close() {
        //todo:

        // if (!_bEnable.exchange(false)) {
        //     return;
        // }
        // if (_pBev.load() != nullptr) {
        //     const static timeval tv = {0, 0};
        //     event_base_once(
        //         _evbase->event_base(), 0, 0, [](evutil_socket_t fd, short nEvent, void* pUserData) {
        //             auto* pClient = static_cast<TCPClient*>(pUserData);
        //             auto pBev = pClient->_pBev.exchange(nullptr);
        //             if (pBev) {
        //                 bufferevent_free(pBev);
        //             }
        //         },
        //         this, &tv);
        // }
        _nReconnTime = 0;
        //TCPSocket::Close();
    }

    void OnError(const std::string& msg) {
        std::cout << msg << std::endl;
    }

    bool Connect(const std::string& strAddr, EventLoopPtr pBase = nullptr) {
        _bNeedBase = pBase == nullptr;

        if (_bNeedBase) {
            pBase = std::make_shared<EventLoopThread>();
            std::static_pointer_cast<EventLoopThread>(pBase)->Start();
        }

        if (!SetRemoteAddress(strAddr)) {
            return false;
        }

        auto _id = Helper::GenSocketID();
        _evbase = pBase;

        if (isAutoReconn()) {
            _pReConnEvent = event_new(pBase->event_base(), -1, EV_PERSIST, &TCPClient::doReconnectCB, this);
            timeval tv = {0, 0};
            return 0 == event_add(_pReConnEvent, &tv);
        } else {
            // bloking connect for first time
            if (!ReConnect()) {
                return false;
            }
        }
        return true;
    }

    bool SetRemoteAddress(const std::string& strAddr) {
        auto tp = Helper::SpilitHostPort(strAddr);
        _host = std::get<0>(tp);
        _hostport = std::get<1>(tp);

        if (_host.empty() || _hostport == 0) {
            return false;
        }

        evutil_addrinfo* ans;
        evutil_addrinfo hints;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_CANONNAME;

        auto status = evutil_getaddrinfo(_host.c_str(), std::to_string(_hostport).c_str(), &hints, &ans);
        if (status != 0 || ans == nullptr) {
            return false;
        }
        _addr = ans;
        // auto nport = htons(_hostport);

        // _kRemoveAddr.sin_family = ans->ai_family;
        // _kRemoveAddr.sin_port = htons(_hostport);
        // memcpy_s(&_kRemoveAddr.sin_addr, sizeof(_kRemoveAddr.sin_addr), ans->ai_addr, sizeof(ans->ai_addrlen));
        // _kRemoveAddr.sin_family = AF_INET;
        // _kRemoveAddr.sin_addr.s_addr = inet_addr(_host.c_str());
        // _kRemoveAddr.sin_port = htons(_hostport);
        // evutil_freeaddrinfo(ans);
        return true;
    }

  protected:
    void OnConnEnable(bool ok) {
        if (ok) {
            if (isAutoReconn()) {
                event_del(_pReConnEvent);
            }
            auto fd = bufferevent_getfd(_evbuff);
            int nodelay = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        }

        //TCPSocket::OnConnEnable(ok);

        if (!ok) {
            if (_evbuff) {
                bufferevent_free(_evbuff);
                _evbuff = nullptr;
            }

            if (isAutoReconn() && _pReConnEvent) {
                timeval tv = {(long)_nReconnTime, 0};
                event_add(_pReConnEvent, &tv);
            }
        }
    }

    bool ReConnect() {

        int code = -1;
        std::string errmsg;

        do {
            _evbuff = bufferevent_socket_new(_evbase->event_base(), -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
            if (_evbuff == nullptr) {
                code = 2;
                break;
            }

            if (0 != bufferevent_socket_connect(_evbuff, _addr->ai_addr, _addr->ai_addrlen)) {
                errmsg = ("bufferevent_socket_connect failed!");
                code = 3;
                break;
            }

            // 这个函数是异步的
            // if (0 != bufferevent_socket_connect_hostname(_evbuff, NULL, AF_INET, _host.data(), _hostport)) {
            //     errmsg = ("bufferevent_socket_connect failed!");
            //     code = 3;
            //     break;
            // }

            code = 0;
        } while (0);

        auto ok = code == 0;
        if (!ok) {
            if (_evbuff) {
                bufferevent_free(_evbuff);
                _evbuff = nullptr;
            }

            OnError(errmsg);

            if (isAutoReconn()) {
                timeval tv = {(long)_nReconnTime, 0};
                event_add(_pReConnEvent, &tv);
            }
        }
        return ok;
    }

    bool isAutoReconn() {
        return _nReconnTime > 0;
    }

    static void doReconnectCB(evutil_socket_t fd, short ev, void* ud) {
        TCPClient* pClient = static_cast<TCPClient*>(ud);
        if (!pClient->ReConnect()) {
            pClient->OnError("reconnect failed");
        }
    }

    TCPConnPtr _conn = nullptr;
    EventLoopPtr _evbase = nullptr;
    bufferevent* _evbuff = nullptr;

    Mutex _lock;

    std::string _host;
    int _hostport = 0;

    bool _bNeedBase = false;
    std::atomic<uint32_t> _nReconnTime = {2};
    event* _pReConnEvent = nullptr;
    sockaddr_in _kRemoveAddr;

    evutil_addrinfo* _addr = nullptr;
};
} // namespace base_on_libevent
