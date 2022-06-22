#include "cache_service.h"
#include <assert.h>

int main(int argc, char *argv[])
{
    Config config;
    config.parse_arg(argc, argv);
    CacheServer server;
    server.init();
    server.setPort(config.PORT);
    int ret;
    ret = server.connToMaster();
    assert(ret == 0);
    ret = server.startListen();
    assert(ret == 0);
    server.eventLoop();
}