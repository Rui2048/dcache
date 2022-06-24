#include "master_service.h"
#include <string.h>
#include <assert.h>

int main()
{
    MasterServer server;
    int ret;
    ret = server.init();
    assert(ret == 0);
    ret = server.startListen();
    assert(ret == 0);
    server.EventLoop(); //开启事件循环
    printf("异常退出\n");
    getchar();
}