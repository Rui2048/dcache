#include "cache_service.h"
#include <assert.h>
#include <iostream>
#include <string.h>
#include <unistd.h>

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
    Log::get_instance()->init("./log", m_close_log, 8000, 50000, 0);
    //初始化线程池
    threadInit(m_thread_number, m_max_requests);

    LOG_INFO("cache_server init sucessfully!");
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
