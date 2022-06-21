#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <string>
#include <sys/socket.h>


class EndPoint
{
    public:
    std::string ip;
    int port;
    EndPoint(std::string _ip, int _port) : ip(_ip), port(_port) {}
    bool operator <(const EndPoint &p) {return ip < p.ip;}
};

static int str2port(std::string port_str)
{
    int port = 0;
    for (char ch : port_str)
    {
        if (ch >= '0' && ch <= '9')
        {
            port = port * 10 + (int)(ch - '0');
        }
        else
            return -1;
    }
    return port;
}

static std::string port2str(int port)
{
    std::string port_str;
    int p = port;
    while (p) {
        port_str = (char)(p % 10 + '0') + port_str;
        p /= 10;
    }
    return port_str;
}

static std::string sock_addr2str(sockaddr_in addr)
{
    std::string addr_str = inet_ntoa(addr.sin_addr);
    addr_str += ':';
    addr_str += port2str(addr.sin_port);
    return addr_str;
}

static std::string endpoint2str(EndPoint point)
{
    std::string res(point.ip);
    res += ':';
    res += port2str(point.port);
    return res;
}

static EndPoint str2endpoint(std::string ip_port)
{
    std::string ip;
    int port = -1;
    EndPoint res("", -1);
    if (!ip_port.empty())
    {
        
        int i = 0;
        while (i < ip_port.size() && ip_port[i] != ':')
            ip += ip_port[i++];
        i++;
        std::string port_str;
        while (i < ip_port.size())
            port_str += ip_port[i++];
        port = str2port(port_str);
        if (port != 0)
        {
            res.ip = ip;
            res.port = port;
        }
    }
    return res;
}



#endif //ENDPOINT_H