#include <arpa/inet.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //for sleep()
#include <iostream>

int main()
{
    int cfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (cfd == -1) 
    {
        printf("cfd create failed\n");
        return -1;
    }
    //设置服务器ip和端口
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(7000);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr);

    //设置客户端的ip
    sockaddr_in client_addr; //不手动绑定，随机生成
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(6000);
    inet_pton(AF_INET, "127.0.0.2", &client_addr.sin_addr.s_addr);
    int ret = bind(cfd, (sockaddr*)(&client_addr), sizeof(client_addr));
    if (ret == -1)
    {
        printf("udp addr bind failed, errno is %d", errno);
        return -1; 
    }
    
    char buf[1024];

    int num = 0;
    while (1) {
        std::cin>>buf;
        //sprintf(buf, "hello udp %d\n", num++);
        sendto(cfd, buf, strlen(buf) + 1, 0, (sockaddr*)(&server_addr), sizeof (server_addr));

        memset(buf, 0, sizeof(buf));
        sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int rlen = recvfrom(cfd, buf, sizeof(buf), 0, (sockaddr*)(&addr), &addr_len);
        char *ip_port = inet_ntoa(addr.sin_addr);
        if (rlen > 0)
            printf("from server %s:%d, ret : %s", ip_port, ntohs(addr.sin_port), buf);
        else
            printf("nothing received...\n");
        sleep(1);
    }

}