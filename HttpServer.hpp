#pragma once

#include <thread>
#include <unordered_map>
#include <functional>
#include <future>
#include <atomic>

extern "C" {
#include <event2/http.h>
}

#include "EventLoop.hpp"

namespace base_on_libevent {
class HttpServer {
  public:
    class Request {
      public:
        friend class HttpServer;

        Request(evhttp_request* req) {
            _evreq = req;
        }

        std::string Path() {
            return evhttp_uri_get_path(evhttp_request_get_evhttp_uri(_evreq));
        }

        int Method() {
            return evhttp_request_get_command(_evreq);
        }

        void ReadHeader(std::unordered_map<std::string, std::string>& out) {
            auto hs = evhttp_request_get_input_headers(_evreq);
            for (auto header = hs->tqh_first; header; header = header->next.tqe_next) {
                out[header->key] = header->value;
            }
        }

        void ReadBodyTo(std::string& body) {
            auto buf = evhttp_request_get_input_buffer(_evreq);
            auto buflen = evbuffer_get_length(buf);
            auto pRaw = evbuffer_pullup(buf, buflen);
            body.assign((char*)pRaw, buflen);
        }
        evhttp_request* _evreq = nullptr;
    };

    class Response {
      public:
        Response(evhttp_request* req)
            : _evreq(req) {
        }
        void Reply(int code, const std::string& body) {
            if (body.size() > 0) {
                Write(body);
            }
            evhttp_send_reply(_evreq, code, NULL, NULL);
        }

        void Write(const std::string& body) {
            auto out = evhttp_request_get_output_buffer(_evreq);
            evbuffer_add(out, body.c_str(), body.size());
        }

        void SetHeader(const std::string& key, const std::string& v) {
            if (key.size() > 0) {
                auto hs = evhttp_request_get_output_headers(_evreq);
                evhttp_add_header(hs, key.c_str(), v.c_str());
            }
        }
        evhttp_request* _evreq = nullptr;
    };

    using ServerHttpFunc = std::function<void(Request&, Response&)>;

    HttpServer(int threadnum, EventLoopPtr loop = nullptr)
        : _threads(threadnum) {
        _run_status = 0;
        _loop = loop;
        _createEVBaseLocal = _loop == nullptr;
        if (_createEVBaseLocal) {
            auto pLoop = std::make_shared<EventLoopThread>();
            pLoop->Start();
            _loop = pLoop;
        }
    }

    virtual ~HttpServer() {
        Stop();
        _threads.Stop();
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

    bool Start(uint32_t port, ServerHttpFunc cb) {
        auto status = _run_status.exchange(1);
        if (status != 0) {
            return false;
        }

        _cbfunc = cb;

        const char* addr = "0.0.0.0";

        auto pEvHttp = evhttp_new(_loop->event_base());
        if (pEvHttp == nullptr) {
            return false;
        }
        evhttp_set_gencb(pEvHttp, _handle, this);

        _evhttp = std::shared_ptr<evhttp>(pEvHttp, [](evhttp* p) {
            evhttp_free(p);
        });

        auto handle = evhttp_bind_socket_with_handle(pEvHttp, addr, port);
        if (handle == nullptr) {
            return false;
        }
        _threads.Start();
        return true;
    }

    EventLoopPtr NextWorker() {
        if (_threads.Size() == 0) {
            return _loop;
        }
        return _threads.GetNextLoop();
    }

  private:
    void OnHandle(EventLoopPtr evloop, struct evhttp_request* evreq) {
        std::shared_ptr<Request> pReq = std::make_shared<Request>(evreq);
        std::shared_ptr<Response> pResp = std::make_shared<Response>(evreq);

        if (_cbfunc) {
            _cbfunc(*pReq, *pResp);
        } else {
            pResp->Reply(HTTP_NOTFOUND, "");
        }
    }

    static void _handle(struct evhttp_request* req, void* h) {
        auto p = (HttpServer*)h;
        auto evbase = p->NextWorker();
        evbase->RunInLoop(std::bind(&HttpServer::OnHandle, p, evbase, req));
    }

    EventLoopThreadPool _threads;
    EventLoopPtr _loop = nullptr;
    std::atomic<uint8_t> _run_status;
    ServerHttpFunc _cbfunc = nullptr;
    bool _createEVBaseLocal = false;
    std::shared_ptr<evhttp> _evhttp = nullptr;
};
} // namespace base_on_libevent