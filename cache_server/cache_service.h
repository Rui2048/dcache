#ifndef CACHE_SERVICE
#define CACHE_SERVICE

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <list>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <thread>
#include <unistd.h>
#include <stdlib.h>
#include "../endPoint.h"
#include "../log/log.h"
#include "../consistent_hash.h"
#include "lru.h"
#include "../config.h"

class CacheServer;

//cache_server类
class CacheServer
{
    public:
    CacheServer();
    ~CacheServer();
    //初始化cache_server
    int init();
    void setPort(int port) {cache_server_port = port;}  //设置监听端口
    int connToMaster();  //连接到master_server -- 注册自己
    int startListen();  //开启监听
    int eventLoop();  //开启事件循环
    int getValue(std::string key, int cfd, std::string client_addr);
    int setValue(std::string key, std::string value, std::string client_addr);
    int getBackupValue(std::string key, int cfd, std::string client_addr);
    int setBackupValue(std::string key, std::string value, std::string client_addr);
    int updateHash(std::vector<std::string> ip_port_str);
    static void *updateBackup(CacheServer *server, std::string ip_port);
    void update(std::string ip_port);
    int recoverBackup();
    static void *move_worker(CacheServer *server);
    void move();
    int dealwithMasterMsg();
    int dealwithClientMsg(int cfd);
    int dealwithListenMsg();

    //LRU
    int lru_size = 5;
    ThreadSafeLRUCache<std::string, std::string> lru;
    ThreadSafeLRUCache<std::string, std::string> lru_backup;
    locker lru_mutex; //move的时候不能set
    locker lru_backup_mutex; //重建备份的时候不要读备份表
    int state = 0; //0:移动lru   1:移动lru_backup
    int backup_invalid = 0; //0:备份有效   1:备份失效


    //一致性哈希
    con_hash *cacheServerHash;
    locker hash_mutex;
    
    //epoll相关
    private:
    int epfd; //epoll文件描述符
    int epollAddfd(int fd); //向epoll中添加文件描述符
    int epollDelfd(int fd); //从epoll中删除文件描述符

    //日志
    private:
    int m_close_log = 0;  // 0:开启日志  1:关闭日志
    //int m_log_write_model = 0;  // 0:异步日志  1:同步日志

    //与master_server连接相关
    int master_sockfd;
    sockaddr_in master_server_sockaddr;
    int master_port = 7010; //master_server的端口

    //与client连接, 更新备份
    int lfd;
    sockaddr_in cache_server_sockaddr; //cache_server本地监听地址
    int cache_server_port; //cache_server本地监听默认端口6000
    std::string local_addr;

    std::map<int, std::string> connectedMap;
    //unordered_set<int> sockfd_set;
    locker sockfd_mutex;

    //线程池
    private:
    //工作队列中的数据类型，client_addr和key
    struct Request
    {
        std::string request_type;
        std::string client_addr; //发送请求的客户端地址结构
        std::string key;  //请求查询的key
        std::string value;
        int cfd;
        Request(std::string type, std::string addr, std::string k, std::string v, int fd = -1) :
        request_type(type), 
        client_addr(addr), 
        key(k), value(v), cfd(fd) {}
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