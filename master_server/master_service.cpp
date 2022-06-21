#include "master_service.h"
#include <assert.h>
#include <iostream>
#include <string.h>
#include <unistd.h>

MasterServer::~MasterServer()
{
    close(lfd);
    close(epfd);
    delete[] m_threads;
}

int MasterServer::init()
{
    //初始化日志：默认为异步日志,阻塞队列的容量为100
    Log::get_instance()->init("./log", m_close_log, 8000, 50000, 0);
    //初始化线程池
    threadInit(m_thread_number, m_max_requests);

    LOG_INFO("master_server init sucessfully!");
}

int MasterServer::epollAddfd(int fd)
{
    assert(epfd != -1);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1)
    {
        std::cout << "epoll add fd fail on fd : " << fd << std::endl;
        return -1;
    }
    return 0;
}

int MasterServer::epollDelfd(int fd)
{
    assert(epfd != -1);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &event);
    if (ret == -1)
    {
        std::cout << "epoll delete fd fail on fd : " << fd << std::endl;
        return -1;
    }
    return 0;
}

int MasterServer::GetDistribute(sockaddr_in client_addr, std::string key)
{
    std::string client_addr_str = sock_addr2str(client_addr);
    std::string ip_port = masterHash.conhash_get_CacheServer(key);
    char buf[1024];
    strcpy(buf, ip_port.c_str());
    if (ip_port == "NULL") {
        LOG_WARN("client %s get distribute failed, conhash is empty", client_addr_str.c_str());
    }
    else {
        LOG_INFO("client %s get distribute success, return %s", client_addr_str.c_str(), ip_port.c_str());
    }
    sendto(master_client_sockfd, buf, strlen(buf), 0, (sockaddr*)(&client_addr), sizeof(client_addr));
    //sendto(master_client_sockfd, msg, strlen(msg) + 1, 0, (sockaddr*)&client_udp_addr, sizeof(client_udp_addr));
    return 1;
}

int MasterServer::Register(EndPoint cache_server, int cfd)
{
    std::string cache_server_str = endpoint2str(cache_server);
    //1、在连接列表中插入新的连接
    map<std::string, int> notity = connectionMap; //用于通知迁移数据
    m_conn_lock.lock();
    auto ret = connectionMap.insert({cache_server_str, cfd});
    m_conn_lock.unlock();
    if (ret.second == false) 
    {
        LOG_ERROR("connctionMap insert failed on cache server : %s : %d", cache_server.ip.c_str(), cache_server.port);
        return -1;
    }
    //2、通知迁移数据
    for (auto iter : notity) 
    {
        int fd = iter.second;
        char buf[1024];
        strcpy(buf, cache_server_str.c_str());
        send(fd, buf, sizeof(buf), 0);
    }
    //3、将新的cache_server添加到一致性哈希
    masterHash.conhash_add_CacheServer(cache_server_str);
    //std::cout << cache_server_str << std::endl;
    LOG_INFO("new cache_server regist : %s", cache_server_str.c_str());
    return 1;
}

int MasterServer::HeartBeat()
{
    
}

int MasterServer::EventLoop()
{
    master_client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (master_client_sockfd == -1) 
    {
        LOG_ERROR("master_client socket create failed");
        return -1; 
    }
    master_udp_addr.sin_family = AF_INET;
    master_udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    master_udp_addr.sin_port = htons(port_udp);

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        LOG_ERROR("lfd create failed");
        return -1;
    }
    master_tcp_addr.sin_family = AF_INET;
    master_tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    master_tcp_addr.sin_port = htons(port_tcp);
    //打开端口复用
    int opt = 1;
    setsockopt(master_client_sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));

    //udp绑定
    int ret = bind(master_client_sockfd, (sockaddr*)(&master_udp_addr), sizeof(master_udp_addr));
    if (ret == -1)
    {
        LOG_ERROR("udp addr bind failed, errno is %d", errno);
        return -1; 
    }
    //tcp绑定
    ret = bind(lfd, (sockaddr*)(&master_tcp_addr), sizeof(master_tcp_addr));
    if (ret == -1)
    {
        LOG_ERROR("tcp addr bind failed, errno is %d", errno);
        return -1; 
    }
    ret = listen(lfd, 5);
    if (ret == -1)
    {
        LOG_ERROR("tcp listen failed");
        return -1; 
    }

    epfd = epoll_create(5);
    if (epfd == -1) 
    {
        LOG_ERROR("epoll init failed");
        return -1; 
    }
    ret = epollAddfd(master_client_sockfd);
    if (ret == -1) 
    {
        LOG_ERROR("epoll add master_client_sockfd failed");
        return -1; 
    }
    ret = epollAddfd(lfd);
    if (ret == -1) 
    {
        LOG_ERROR("epoll add lfd failed");
        return -1; 
    }

    epoll_event evs[1024];
    int ev_size = sizeof(evs) / sizeof(epoll_event);
    char buf[1024];
    int len = sizeof(client_udp_addr);
    while (!shutdown) 
    {
        int num = epoll_wait(epfd, evs, ev_size, -1);
        for (int i = 0; i < num; i++) 
        {
            int cur_fd = evs[i].data.fd;
            //处理client的请求
            if (cur_fd == master_client_sockfd)
            {
                int rlen = recvfrom(cur_fd, buf, sizeof(buf), 0, (sockaddr*)(&client_udp_addr), (socklen_t*)&len);
                std::string ip_port = sock_addr2str(client_udp_addr);
                //std::cout << ip_port << std::endl;
                //LOG_INFO("receive from %s request key : %s", ip_port.c_str(), buf);;
                
                std::string key = buf;
                bool ret = append(Request(GETDISTRIBUTE, client_udp_addr, key)); //将客户端请求加入请求队列
                if (ret ==false) 
                {
                    LOG_ERROR("threadpool append failed on getdistribute request from %s with key %s", ip_port, buf);
                    return -1;
                }

                //调试用，给客户端一个返回的信息
                /*char msg[1024];
                sprintf(msg, "I have received your msg : ");
                strcpy(msg, buf);
                sendto(master_client_sockfd, msg, strlen(msg) + 1, 0, (sockaddr*)&client_udp_addr, sizeof(client_udp_addr));*/
            }
            //新的cache_server请求注册
            else if (cur_fd == lfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int cfd = accept(cur_fd, (struct sockaddr *)&client_address, &client_addrlength);
                if (cfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    return -1;
                }
                std::string ip_port = sock_addr2str(client_address);
                bool ret = append(Request(REGISTER, client_address, "", cfd)); //将客户端请求加入请求队列
                if (ret ==false) 
                {
                    LOG_ERROR("threadpool append failed on register request from %s with key %s", ip_port, buf);
                    return -1;
                }
            }
        }
    }
}

//初始化线程池
int MasterServer::threadInit(int thread_number, int max_requests)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
/*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
void *MasterServer::worker(void *arg)
{
    MasterServer *Server = (MasterServer*)arg;
    Server->run();
    return Server;
}
void MasterServer::run()
{
    while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        Request request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        switch (request.request_type)
        {
        case GETDISTRIBUTE:
        {
            GetDistribute(request.client_addr, request.key);
            break;
        }
        case REGISTER:
        {
            //std::cout << "register" << std::endl;
            std::string ip_port = sock_addr2str(request.client_addr);
            EndPoint point = str2endpoint(ip_port);
            Register(point, request.cfd);
            break;
        }
        default:
            break;
        }
    }
}
//往工作队列中添加任务
bool MasterServer::append(Request request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
