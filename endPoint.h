#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <string>


struct EndPoint
{
    std::string ip;
    int port;
    EndPoint(std::string _ip, int _port) : ip(_ip), port(_port) {}
};

std::string Endpoint2Str(EndPoint point)
{
    std:string res(point.ip);
    res += ':';
    std::string port;
    int p = point.port;
    while (p) {
        port = (char)(p % 10 + '0') + port;
        p /= 10;
    }
    res += port;
    return res;
}



#endif //ENDPOINT_H