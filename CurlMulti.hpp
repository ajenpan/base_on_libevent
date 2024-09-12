#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

extern "C" {
#include <curl/curl.h>
}

#include "EventLoop.hpp"

namespace base_on_libevent {

namespace detail {
struct ci {
    bool operator()(const std::string& s1, const std::string& s2) const {
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
                                            [](unsigned char c1, unsigned char c2) {
                                                return ::tolower(c1) < ::tolower(c2);
                                            });
    }
};
} // namespace detail

class CurlMulti {
  public:
    // using Headers = std::multimap<std::string, std::string, detail::ci>;
    using Headers = std::vector<std::string>;

    struct Response {
        int httpcode = -1;
        std::string body;
        std::string head;
        CURL* curl = nullptr;

        char* GetEffectiveUrl() const {
            char* done_url = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &done_url);
            return done_url;
        }
        double GetTotalTime() {
            double cost_time = 0;
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &cost_time);
            return cost_time;
        }
    };
    using ResponsePtr = std::shared_ptr<Response>();

    using CallbackFunc = std::function<void(Response*)>;

    struct Request {
        std::string method;
        std::string url;
        std::string body;
        Headers headers;
        CallbackFunc cb = nullptr;
    };

    using RequestPtr = std::shared_ptr<Request>();

    struct CurlContext {
        CURL* curl = nullptr;
        std::shared_ptr<Request> req;
        std::shared_ptr<Response> resp;
    };

    struct SocketContext {
        event ev;
        curl_socket_t fd;
        CurlMulti* base;
    };

    CurlMulti(EventLoopPtr base = nullptr) {
        _evbase = base;

        _createEVBaseLocal = _evbase == nullptr;
        if (_createEVBaseLocal) {
            _evbase = std::make_shared<EventLoopThread>();
            std::static_pointer_cast<EventLoopThread>(_evbase)->Start();
        }

        evtimer_assign(&_timeout, _evbase->event_base(), OnTimeout, this);

        _curlm = curl_multi_init();
        curl_multi_setopt(_curlm, CURLMOPT_SOCKETFUNCTION, HandleSocket);
        curl_multi_setopt(_curlm, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(_curlm, CURLMOPT_TIMERFUNCTION, SetTimeout);
        curl_multi_setopt(_curlm, CURLMOPT_TIMERDATA, this);
        // curl_multi_setopt(_curlm, CURLMOPT_MAXCONNECTS, 1000L);
    }

    virtual ~CurlMulti() {
        if (_createEVBaseLocal) {
            _evbase->Stop();
            _evbase = nullptr;
        }
        curl_multi_cleanup(_curlm);
        event_del(&_timeout);
    }

    bool Post(const std::string& url, std::vector<std::string> header, const std::string& data, CallbackFunc cb) {
        auto req = std::make_shared<Request>();
        req->url = url;
        req->headers.swap(header);
        req->body = data;
        req->cb = cb;
        req->method = "POST";
        return CallRequest(req);
    }

    bool Get(const std::string& url, CallbackFunc cb) {
        auto req = std::make_shared<Request>();
        req->url = url;
        req->cb = cb;
        req->method = "GET";
        return CallRequest(req);
    }

    bool CallRequest(std::shared_ptr<Request> req) {
        return DoCall(AllocContext(req));
    }

    bool DoCall(CurlContext* ctx) {
        bool ret = _evbase->RunInLoop([this, ctx]() {
            curl_multi_add_handle(_curlm, ctx->curl);
        });
        if (!ret) {
            FreeContext(ctx);
            return false;
        }
        return ret;
    }

    CurlContext* AllocContext(std::shared_ptr<Request> req) {
        // todo: use obj pool
        auto ctx = new CurlContext();

        auto curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method.c_str());
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CurlMulti::OnRespHead);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, ctx);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlMulti::OnRespBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_PRIVATE, ctx);
        curl_easy_setopt(curl, CURLOPT_URL, req->url.c_str());
        if (req->body.size() > 0) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body.size());
        }

        struct curl_slist* chunk = nullptr;
        for (auto& head : req->headers) {
            chunk = curl_slist_append(chunk, head.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        ctx->curl = curl;
        ctx->req = req;

        if (req->cb) {
            ctx->resp = std::make_shared<Response>();
            ctx->resp->curl = curl;
        }
        return ctx;
    }

    void FreeContext(CurlContext* ctx) {
        if (ctx->curl != nullptr) {
            curl_easy_cleanup(ctx->curl);
            ctx->curl = nullptr;
        }
        delete ctx;
    }

    EventLoopPtr EventBase() {
        return _evbase;
    }

  protected: //static function
    static SocketContext* CreateSocketContext(CurlMulti* e, curl_socket_t sockfd) {
        // todo: use obj pool
        SocketContext* sctx = new SocketContext;
        sctx->fd = sockfd;
        sctx->base = e;

        event_assign(&sctx->ev, e->_evbase->event_base(), sockfd, 0, CurlPerform, sctx);

        return sctx;
    }

    static void DestroySocketContext(SocketContext* sctx) {
        event_del(&sctx->ev);
        delete sctx;
    }

    static void CheckMultiInfo(CurlMulti* base) {
        int pending;
        auto& curlm = base->_curlm;

        while (true) {
            CURLMsg* message = curl_multi_info_read(curlm, &pending);
            if (message == nullptr) {
                break;
            }
            if (message->msg != CURLMSG_DONE) {
                continue;
            }

            CURL* curl = message->easy_handle;
            curl_multi_remove_handle(curlm, curl);

            CurlMulti::CurlContext* ctx = nullptr;
            curl_easy_getinfo(curl, CURLINFO_PRIVATE, &ctx);
            if (ctx == nullptr) {
                break;
            }

            if (ctx->req->cb && ctx->resp) {
                auto& resp = ctx->resp;

                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->httpcode);

                ctx->req->cb(resp.get());
            }

            base->FreeContext(ctx);
        }
    }

    static void CurlPerform(evutil_socket_t fd, short event, void* arg) {
        int flags = 0;

        if (event & EV_READ)
            flags |= CURL_CSELECT_IN;
        if (event & EV_WRITE)
            flags |= CURL_CSELECT_OUT;

        SocketContext* sctx = (SocketContext*)arg;

        // NOTE: you need copy the base point, because sctx will be deleted in curl_multi_socket_action func;
        CurlMulti* base = sctx->base;
        CURLM* curl_handle = base->_curlm;

        int running_handles;
        auto code = curl_multi_socket_action(curl_handle, sctx->fd, flags, &running_handles);
        CheckMultiInfo(base);
    }

    static void OnTimeout(evutil_socket_t, short, void* arg) {
        CurlMulti* base = (CurlMulti*)arg;
        auto& curl_handle = base->_curlm;
        int running_handles = 0;
        curl_multi_socket_action(curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
        CheckMultiInfo(base);
    }

    static int HandleSocket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
        CurlMulti* e = (CurlMulti*)userp;
        auto& curlbase = e->_curlm;
        auto evbase = e->_evbase->event_base();

        SocketContext* sctx = (SocketContext*)socketp;

        switch (action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT: {
            if (sctx == nullptr) {
                sctx = CreateSocketContext(e, s);
            } else {
                event_del(&sctx->ev);
            }

            curl_multi_assign(curlbase, s, (void*)sctx);

            int events = 0;
            if (action != CURL_POLL_IN)
                events |= EV_WRITE;
            if (action != CURL_POLL_OUT)
                events |= EV_READ;
            events |= EV_PERSIST;

            event_assign(&sctx->ev, evbase, sctx->fd, events, CurlPerform, (void*)sctx);
            event_add(&sctx->ev, NULL);

        } break;
        case CURL_POLL_REMOVE: {
            if (sctx) {
                DestroySocketContext(sctx);
                curl_multi_assign(curlbase, s, NULL);
            }
        } break;
        default:
            // do nothing
            break;
        }
        return 0;
    }

    static size_t OnRespHead(char* data, size_t sd, size_t n, void* ud) {
        auto realsize = sd * n;
        if (realsize == 0 || !data) {
            return 0;
        }
        CurlContext* ctx = (CurlContext*)ud;
        if (ctx && ctx->resp) {
            ctx->resp->head.append(data, realsize);
        }
        return realsize;
    }

    static size_t OnRespBody(char* data, size_t sd, size_t n, void* ud) {
        auto realsize = sd * n;
        if (realsize == 0 || !data) {
            return 0;
        }
        CurlContext* ctx = (CurlContext*)ud;
        if (ctx && ctx->resp) {
            ctx->resp->body.append(data, realsize);
        }
        return realsize;
    }

    static int SetTimeout(CURLM* multi, long timeout_ms, void* userp) {
        CurlMulti* e = (CurlMulti*)userp;
        auto& timeout = e->_timeout;

        if (timeout_ms < 0) {
            event_del(&timeout);
        } else {
            if (timeout_ms == 0) {
                // 0 means directly call socket_action, but we will do it in a bit
                timeout_ms = 1;
            }
            timeval tv{
                (long)timeout_ms / 1000,
                (long)(timeout_ms % 1000) * 1000,
            };
            event_del(&timeout);
            event_add(&timeout, &tv);
        }
        return 0;
    }

  private:
    EventLoopPtr _evbase = nullptr;
    bool _createEVBaseLocal = false;

    CURLM* _curlm = nullptr;
    struct event _timeout;
};

} // namespace base_on_libevent
