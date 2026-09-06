#include "../src/beast.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

using namespace fcitx;

FCITX_DEFINE_STATIC_ADDON_REGISTRY(getStaticAddon)
FCITX_IMPORT_ADDON_FACTORY(getStaticAddon, beast);

int main() {
    char arg0[] = "testbeast";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=beast";
    char *argv[] = {arg0, arg1, arg2};
    Log::setLogRule("default=5");

    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(&getStaticAddon());
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());

    instance.initialize();
    auto beast = dynamic_cast<Beast *>(instance.addonManager().addon("beast"));
    assert(beast);

    bool getterCalled = false;
    bool setterCalled = false;
    bool remoteCalled = false;

    beast->setConfigGetter([&](const char *) {
        getterCalled = true;
        return "";
    });
    beast->setConfigSetter([&](const char *, const char *) {
        setterCalled = true;
        return "";
    });
    beast->setRemoteHandler([&](const std::string_view,
                                const char *) -> std::pair<bool, std::string> {
        remoteCalled = true;
        return {true, ""};
    });

    // Listen on default unix socket.
    assert(std::system("lsof /tmp/fcitx5.sock") == 0);

    // Getter works.
    assert(!getterCalled);
    assert(std::system("curl --unix-socket /tmp/fcitx5.sock "
                       "http://fcitx/config/addon/beast") == 0);
    assert(getterCalled);

    // Setter works.
    assert(!setterCalled);
    assert(std::system("curl --unix-socket /tmp/fcitx5.sock -X POST -d '{}' "
                       "http://fcitx/config/addon/beast") == 0);
    assert(setterCalled);

    // Remote works.
    assert(!remoteCalled);
    assert(std::system("curl --unix-socket /tmp/fcitx5.sock -X POST "
                       "http://fcitx/remote/c") == 0);
    assert(remoteCalled);

    // Unix socket path reset works.
    auto config = dynamic_cast<const BeastConfig *>(beast->getConfig());
    RawConfig raw;
    config->dumpDescription(raw);
    BeastConfig cfg;
    cfg.unix_socket.mutableValue()->path.setValue("/tmp/fcitx5-another.sock");
    cfg.save(raw);
    beast->setConfig(raw);

    assert(std::system("lsof /tmp/fcitx5.sock") != 0);
    assert(std::system("lsof /tmp/fcitx5-another.sock") == 0);

    // Tcp default port works.
    cfg.communication.setValue(BeastCommunication::TCP);
    cfg.save(raw);
    beast->setConfig(raw);
    assert(std::system("lsof -i:32489") == 0);

    getterCalled = false;
    setterCalled = false;
    remoteCalled = false;

    // Getter works over TCP.
    assert(!getterCalled);
    assert(std::system("curl "
                       "http://127.0.0.1:32489/config/addon/beast") == 0);
    assert(getterCalled);

    // Setter works over TCP.
    assert(!setterCalled);
    assert(std::system("curl -X POST -d '{}' "
                       "http://127.0.0.1:32489/config/addon/beast") == 0);
    assert(setterCalled);

    // Remote works over TCP.
    assert(!remoteCalled);
    assert(std::system("curl -X POST "
                       "http://127.0.0.1:32489/remote/c") == 0);
    assert(remoteCalled);

    // Tcp port reset works.
    cfg.tcp.mutableValue()->port.setValue(32490);
    cfg.save(raw);
    beast->setConfig(raw);

    assert(std::system("lsof -i:32489") != 0);
    assert(std::system("lsof -i:32490") == 0);

    return 0;
}
