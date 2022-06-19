#include "master_service.h"
#include <string.h>

int main()
{
    MasterServer server;
    server.init();
    server.EventLoop();
}