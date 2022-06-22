#include "cache_service.h"
#include <assert.h>
#include <iostream>
#include <string.h>

CacheServer::CacheServer() : 
lru(lru_size), lru_backup(lru_size)
{
    
}
CacheServer::~CacheServer()
{

}

int CacheServer::epollAddfd(int fd)
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

int CacheServer::epollDelfd(int fd)
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

int CacheServer::init()
{
    //初始化日志：默认为异步日志,阻塞队列的容量为100
    Log::get_instance()->init("./Log/log", m_close_log, 8000, 50000, 0);
    //初始化线程池
    threadInit(m_thread_number, m_max_requests);
    //初始化epoll
    epfd = epoll_create(5);
    if (epfd == -1) 
    {
        LOG_ERROR("epoll init failed");
        return -1; 
    }
    LOG_INFO("cache_server init sucessfully!");
}

int CacheServer::connToMaster()
{
    master_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    master_server_sockaddr.sin_family = AF_INET;
    master_server_sockaddr.sin_port = htons(master_port);
    inet_pton(AF_INET, "127.0.0.1", (void*)(&master_server_sockaddr.sin_addr.s_addr));
    int ret = connect(master_sockfd, (sockaddr*)(&master_server_sockaddr), sizeof(master_server_sockaddr));
    if (ret == -1)
    {
        LOG_ERROR("connect to master failed, errno is %d", errno);
        return -1;
    }
    //sockfd_set.insert(master_sockfd);
    ret = epollAddfd(master_sockfd);
    if (ret == -1) return -1;
    LOG_ERROR("connect to master success");
    char buf[1024];
    sprintf(buf, "%d", cache_server_port);
    send(master_sockfd, buf, strlen(buf), 0);
    return 0;
}

int CacheServer::startListen()
{
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        LOG_ERROR("lfd create failed");
        return -1;
    }
    sockaddr_in cache_server_addr;
    cache_server_sockaddr.sin_family = AF_INET;
    cache_server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    cache_server_sockaddr.sin_port = htons(cache_server_port);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));
    int ret = bind(lfd, (sockaddr*)(&cache_server_sockaddr), sizeof(cache_server_sockaddr));
    if (ret == -1)
    {
        LOG_ERROR("listen sockfd bind failed, errno is %d", errno);
        return -1; 
    }
    ret = listen(lfd, 5);
    if (ret == -1)
    {
        LOG_ERROR("TCP start listen failed, errno is %d", errno);
        return -1; 
    }
    epollAddfd(lfd);
    if (ret == -1) return -1;
    LOG_ERROR("cache_server start listen success");
    return 0;
}

int CacheServer::eventLoop()
{
    epoll_event evs[1024];
    int ev_size = sizeof(evs) / sizeof(epoll_event);
    char buf[1024];
    while (!shutdown)
    {
        int num = epoll_wait(epfd, evs, ev_size, 0);  //不阻塞
        if (num == -1)
        {
            LOG_ERROR("epoll waited failed");
            return -1;
        }
        for (int i = 0; i < num; i++) 
        {
            int cur_fd = evs[i].data.fd;
            if (cur_fd == master_sockfd) //处理来自master的指令
            {
                if (dealwithMasterMsg(cur_fd) == -1)
                    break;
            }
            else if (cur_fd == lfd) //新的客户端请求或者更新backup
            {
                if (dealwithListenMsg() == -1)
                    break;
            }
            else //客户端的请求或者数据迁移
            {
                if (dealwithClientMsg(cur_fd) == -1)
                    break;
            }
        }
    }
    return -1;
}

int CacheServer::getValue(std::string key)
{

}

int CacheServer::setValue(std::string key)
{

}

int CacheServer::getBackupValue(std::string key)
{

}

int CacheServer::setBuckupValue(std::string key)
{
    
}

void *CacheServer::move_worker(void *arg)
{   
    Arg_move *arg_ = (Arg_move*)arg;
    arg_->server->move(arg_->ip_port_str);
}

void CacheServer::move(std::vector<std::string> ip_port_str)
{

}

int CacheServer::dealwithMasterMsg(int cur_fd)
{
    char buf[1024];
    int len = recv(master_sockfd, buf, sizeof(buf), 0);
    if (len == 0) 
    {
        LOG_ERROR("master_server is crashed !!!");
        epollDelfd(cur_fd);
        return -1;
    }
    else if (len == -1) 
    {
        LOG_ERROR("recv from master_server failed, errno is %d", errno);
        return -1;
    }
    std::string temp;
    int i = 0;
    while (buf[i] != '\n')
        temp += buf[i++];
    if (temp == MOVE_DATA)
    {
        LOG_INFO("start move data %s", buf);
        std::vector<std::string> ip_ports;
        i++;
        temp = "";
        while (i < len) {
            if (buf[i] == '\n') {
                ip_ports.emplace_back(temp);
                temp = "";
            }
            else {
                temp += buf[i];
            }
        }
        
        Arg_move *arg = new Arg_move;
        arg->server = this;
        arg->ip_port_str = ip_ports;
        std::thread t(move_worker, arg);
        t.detach();
    }
    
}

int CacheServer::dealwithClientMsg(int cfd)
{

}

int CacheServer::dealwithListenMsg()
{
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int cfd = accept(lfd, (sockaddr*)(&addr), &addr_len);
    if (cfd < 0)
    {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        return -1;
    }
    sockfd_mutex.lock();
    sockfd_set.insert(cfd);
    sockfd_mutex.unlock();
    return 0;
}


//初始化线程池
int CacheServer::threadInit(int thread_number, int max_requests)
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
void *CacheServer::worker(void *arg)
{
    CacheServer *Server = (CacheServer*)arg;
    Server->run();
    return Server;
}
void CacheServer::run()
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
        
    }
}
//往工作队列中添加任务
bool CacheServer::append(Request request)
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
