#pragma once

#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <thread>
#include <iostream>
#include <cassert>

#if __unix__
#include <netinet/tcp.h> // for TCP_NODELAY
#endif

extern "C" {
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/thread.h>
#include <event2/util.h>
}

#include "Helper.h"
#include "ServerStatus.hpp"

namespace base_on_libevent {

class EventLoop {
  public:
    static bool InitThread() {
        int nInitThread = -1;
#ifdef _WIN32
        nInitThread = evthread_use_windows_threads();
#else
        nInitThread = evthread_use_pthreads();
#endif
        return nInitThread == 0;
    }
    static bool InitNetwork() {
#ifdef _WIN32
        WSADATA wsa_data;
        auto wVersionRequested = MAKEWORD(2, 2);
        return WSAStartup(wVersionRequested, &wsa_data) == 0;
#else
        return true;
#endif
    }

    EventLoop() {
        static std::once_flag f_init_network, f_init_thread;
        std::call_once(f_init_network, []() {
            if (!InitNetwork()) {
                std::cerr << "Failed to initialize network." << std::endl;
            }
        });
        std::call_once(f_init_thread, []() {
            if (!InitThread()) {
                std::cerr << "Failed to initialize threading." << std::endl;
            }
        });
        _status = ServerStatus::Status::kNull;
        struct event_config* cfg = event_config_new();
        // Does not cache time to get a preciser timer
        event_config_set_flag(cfg, EVENT_BASE_FLAG_NO_CACHE_TIME);
        _evbase = event_base_new_with_config(cfg);
        event_config_free(cfg);
    }

    virtual ~EventLoop() {
        if (_evbase != nullptr) {
            event_base_free(_evbase);
            _evbase = nullptr;
        }

#ifdef _WIN32
        WSACleanup();
#endif
    }

    virtual int Run() {
        _status.store(ServerStatus::kRunning);
        _tid = std::this_thread::get_id();
        auto rc = event_base_loop(_evbase, EVLOOP_NO_EXIT_ON_EMPTY);
        _status.store(ServerStatus::kStopped);
        return rc;
    }

    virtual int Stop() {
        return event_base_loopexit(_evbase, nullptr);
    }

    struct event_base* event_base() {
        return _evbase;
    }

    bool RunInLoop(std::function<void()> fn, int after_sec = 0) {
        auto ew = new EventWrap();
        ew->fn = std::move(fn);
        timeval tv{(long)after_sec, 0};
        auto code = event_base_once(_evbase, -1, EV_TIMEOUT, OnEvent, (void*)ew, &tv);
        return code == 0;
    }

    struct EventWrap {
        std::function<void()> fn;
    };

    static void OnEvent(evutil_socket_t fd, short what, void* arg) {
        auto ew = (EventWrap*)arg;
        if (ew->fn) {
            ew->fn();
        }
        delete ew;
    }

  private:
    std::atomic<ServerStatus::Status> _status;
    struct event_base* _evbase;
    std::thread::id _tid;
};

using EventLoopPtr = std::shared_ptr<EventLoop>;

class EventLoopThread : public EventLoop {
  public:
    EventLoopThread()
        : EventLoop() {
    }
    ~EventLoopThread() {
        Join();
    }
    void Start() {
        std::lock_guard<std::mutex> guard(_mutex);
        _thread.reset(new std::thread(std::bind(&EventLoopThread::Run, this)));
    }

    void Join() {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_thread && _thread->joinable()) {
            try {
                _thread->join();
            } catch (const std::system_error& e) {
                std::cout << e.what() << std::endl;
            }
        }
        _thread.reset();
    }

  private:
    std::mutex _mutex;
    std::shared_ptr<std::thread> _thread;
};

using EventLoopThreadPtr = std::shared_ptr<EventLoopThread>;

class EventLoopThreadPool : ServerStatus {
  public:
    EventLoopThreadPool(int threadcnt = 1) {
        for (int i = 0; i < threadcnt; i++) {
            _threads.push_back(std::make_shared<EventLoopThread>());
        }
        _status.exchange(kInitialized);
    }

    void Start() {
        _status.exchange(kRunning);
        for (auto& kitem : _threads) {
            kitem->Start();
        }
    }

    void Stop() {
        auto bf = _status.exchange(kStopping);
        if (bf != kRunning) {
            return;
        }

        for (auto& kitem : _threads) {
            kitem->Stop();
        }

        for (auto& kitem : _threads) {
            kitem->Join();
        }

        _status.exchange(kStopped);
    }

    size_t Size() {
        return _threads.size();
    }

    EventLoopThreadPtr GetNextLoop() {
        if (_threads.empty()) {
            return nullptr;
        }
        size_t next = _next.fetch_add(1);
        next = next % _threads.size();
        return _threads[next];
    }

    std::vector<EventLoopThreadPtr> _threads;
    std::atomic<size_t> _next = {0};
};

} // namespace base_on_libevent