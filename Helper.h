#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <ctime>

#if _WIN32
#pragma comment(lib, "ws2_32.lib")   // Socket编程需用的动态链接库
#pragma comment(lib, "Kernel32.lib") // IOCP需要用到的动态链接库
#pragma comment(lib, "mswsock.lib")
#else
#include <arpa/inet.h>
#endif

namespace base_on_libevent {

using TCPSocketID = uint64_t;

class Helper {
  public:
    static std::tuple<std::string, int32_t> SpilitHostPort(const std::string addr) {
        std::string host;
        int32_t port = 0;
        auto nFind = addr.rfind(":");
        if (nFind != std::string::npos) {
            host = addr.substr(0, nFind);
            port = atoi(addr.c_str() + nFind + 1);
        }
        return std::make_tuple(host, port);
    }

    static TCPSocketID GenSocketID() {
        static std::atomic<uint32_t> s_ID(0);
        auto low = (uint64_t)++s_ID;
        auto high = (uint64_t)std::time(nullptr);
        return (high << 32) | low;
    }

    template <class StringVector, class StringType, class DelimType>
    static void StringSplit(const StringType& str, const DelimType& delims, unsigned int maxSplits, StringVector& ret) {

        if (str.empty()) {
            return;
        }

        unsigned int numSplits = 0;

        // Use STL methods
        size_t start, pos;
        start = 0;

        do {
            pos = str.find_first_of(delims, start);

            if (pos == start) {
                ret.push_back(StringType());
                start = pos + 1;
            } else if (pos == StringType::npos || (maxSplits && numSplits + 1 == maxSplits)) {
                // Copy the rest of the string
                ret.emplace_back(StringType());
                *(ret.rbegin()) = StringType(str.data() + start, str.size() - start);
                break;
            } else {
                // Copy up to delimiter
                // ret.push_back( str.substr( start, pos - start ) );
                ret.push_back(StringType());
                *(ret.rbegin()) = StringType(str.data() + start, pos - start);
                start = pos + 1;
            }

            ++numSplits;

        } while (pos != StringType::npos);
    }
};
} // namespace base_on_libevent
