#include <iostream>

#include "../CurlMulti.hpp"

using namespace base_on_libevent;

void cb(CurlMulti::Response* resp) {
    std::cout << resp->httpcode << std::endl;
    std::cout << resp->head << "\n";
    std::cout << resp->body << std::endl;
}

int main(int argc, char** argv) {
    auto cm = std::make_shared<CurlMulti>();

    cm->Get("https://www.baidu.com/", cb);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    cm->Get("https://www.baidu.com/", cb);

    auto pbase = std::make_shared<EventLoop>();
    pbase->Run();
    pbase->Stop();

    std::string recv;
    std::cin >> recv;
    return 0;
}