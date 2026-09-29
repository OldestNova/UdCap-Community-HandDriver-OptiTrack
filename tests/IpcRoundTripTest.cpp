#include <OptiTrackWire.h>
#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

int main() {
    constexpr char address[] = "ipc:///tmp/udcap-community-test";
    nng_socket publisher{}, subscriber{};
    assert(nng_pub0_open(&publisher) == 0);
    assert(nng_sub0_open(&subscriber) == 0);
    assert(nng_setopt(subscriber, NNG_OPT_SUB_SUBSCRIBE, "", 0) == 0);
    assert(nng_setopt_ms(subscriber, NNG_OPT_RECVTIMEO, 50) == 0);
    assert(nng_listen(publisher, address, nullptr, 0) == 0);
    assert(nng_dial(subscriber, address, nullptr, NNG_FLAG_NONBLOCK) == 0);
    udcap::optitrack::Frame sent{};
    sent.hand = 2;
    std::strcpy(sent.serial, "UDXST400049R");
    sent.joints[0] = {0, 0, 0, 1};
    bool received = false;
    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        assert(nng_send(publisher, &sent, sizeof(sent), NNG_FLAG_NONBLOCK) == 0);
        udcap::optitrack::Frame result{};
        size_t length = sizeof(result);
        if (nng_recv(subscriber, &result, &length, 0) == 0) {
            received = length == sizeof(sent) &&
                result.frameMagic == udcap::optitrack::magic &&
                result.hand == 2 && std::strcmp(result.serial, sent.serial) == 0;
        }
    }
    nng_close(subscriber);
    nng_close(publisher);
    assert(received);
}
