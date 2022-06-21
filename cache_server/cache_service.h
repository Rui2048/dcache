#ifndef CACHE_SERVICE
#define CACHE_SERVICE

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <list>
#include <string>
#include <map>
#include "../endPoint.h"
#include "../log/log.h"
#include "../consistent_hash.h"

//cache_server类
class CacheServer
{
    public:
    CacheServer(){}
    ~CacheServer();
    //初始化cache_server
    int init();
    
    //epoll相关
    private:
    int epfd; //epoll文件描述符
    int epollAddfd(int fd); //向epoll中添加文件描述符
    int epollDelfd(int fd); //从epoll中删除文件描述符

    //日志
    private:
    int m_close_log = 0;  // 0:开启日志  1:关闭日志
    //int m_log_write_model = 0;  // 0:异步日志  1:同步日志

    //线程池
    private:
    //工作队列中的数据类型，client_addr和key
    struct Request
    {
        int request_type;
        sockaddr_in client_addr; //发送请求的客户端地址结构
        std::string key;  //请求查询的key
        int cfd;
        Request(int type, sockaddr_in addr, std::string k, int fd = -1) :
        request_type(type), 
        client_addr(addr), 
        key(k), cfd(fd) {}
    };
    int m_thread_number = 8;        //线程池中的线程数
    int m_max_requests = 20;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<Request> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理

    //初始化线程池
    int threadInit(int thread_number = 8, int max_requests = 20);
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();
    //往工作队列中添加任务
    bool append(Request request);

    //全局
    private:
    bool shutdown = false; //true:关闭服务器  false:不关闭
};


#endif //CACHE_SERVICE