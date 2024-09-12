#pragma once

#include <atomic>

#ifndef H_CASE_STRING_BIGIN
#define H_CASE_STRING_BIGIN(state) switch (state) {
#define H_CASE_STRING(state) \
    case state:              \
        return #state;       \
        break;
#define H_CASE_STRING_END() \
    default:                \
        return "Unknown";   \
        break;              \
        }
#endif

namespace base_on_libevent {

class ServerStatus {
  public:
    enum Status : uint8_t {
        kNull = 0,
        kInitializing = 1,
        kInitialized = 2,
        kStarting = 3,
        kRunning = 4,
        kStopping = 5,
        kStopped = 6,
    };

    std::string StatusToString() const {
        H_CASE_STRING_BIGIN(_status.load());
        H_CASE_STRING(kNull);
        H_CASE_STRING(kInitialized);
        H_CASE_STRING(kRunning);
        H_CASE_STRING(kStopping);
        H_CASE_STRING(kStopped);
        H_CASE_STRING_END();
    }

    bool IsRunning() const {
        return _status.load() == kRunning;
    }

    bool IsStopped() const {
        return _status.load() == kStopped;
    }

    bool IsStopping() const {
        return _status.load() == kStopping;
    }

  protected:
    std::atomic<Status> _status = {kNull};
};
}