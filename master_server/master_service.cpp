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
    Log::get_instance()->init("./Log/log", m_close_log, 8000, 50000, 0);
    //初始化线程池
    threadInit(m_thread_number, m_max_requests);

    LOG_INFO("master_server init sucessfully!");
    return 0;
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
    hash_mutex.lock();
    std::string ip_port = masterHash.conhash_get_CacheServer(key);
    hash_mutex.unlock();
    if (ip_port == "NULL") {
        LOG_WARN("client %s get distribute failed, conhash is empty", client_addr_str.c_str());
    }
    else {
        LOG_INFO("client %s get distribute success, return %s", client_addr_str.c_str(), ip_port.c_str());
    }
    std::string msg;
    msg += ip_port;
    if (connectList.isExisted(ip_port)) {
        msg += '\n';
        msg += connectList.getNext(ip_port); //备份服务器
    }
    char buf[1024];
    strcpy(buf, msg.c_str()); 
    sendto(master_client_sockfd, buf, strlen(buf), 0, (sockaddr*)(&client_addr), sizeof(client_addr));
    return 0;
}

int MasterServer::Register(sockaddr_in cache_server, int cfd)
{
    std::string cache_server_str = sock_addr2str(cache_server);
    std::string newAddr = connectedMap[cfd];
    //1、在连接列表中插入新的连接
    map<std::string, int> notity = registedMap; //用于通知迁移数据
    m_conn_lock.lock();
    auto ret = registedMap.insert({newAddr, cfd});
    listenMap.insert({cfd, cache_server_str});
    connectList.add(newAddr);
    m_conn_lock.unlock();
    if (ret.second == false) 
    {
        LOG_ERROR("registedMap insert failed on cache server : %s", cache_server_str.c_str());
        return -1;
    }
    //2、将新的cache_server添加到一致性哈希
    hash_mutex.lock();
    masterHash.conhash_add_CacheServer(cache_server_str);
    hash_mutex.unlock();
    //4、通知cache_server更新哈希
    UpdateHash();
    usleep(1);
    //3、通知迁移数据
    char buf[1024];
    sprintf(buf, "%s\n", MOVE_DATA);
    for (auto iter : notity) 
    {
        int fd = iter.second;
        send(fd, buf, strlen(buf) + 1, 0);
    }
    usleep(1);
    //5、通知迁移备份数据
    if (connectList.isExisted(newAddr))
    {
        //LOG_DEBUG("start notice update backup");
        std::string prev_server_addr = connectList.getPrev(newAddr);
        std::string next_server_addr = connectList.getNext(newAddr);
        int prev_fd = registedMap[prev_server_addr];
        int next_fd = registedMap[next_server_addr];
        //LOG_DEBUG("prev_server %s", prev_server_addr.c_str());
        //LOG_DEBUG("next_server %s", next_server_addr.c_str());
        if (next_fd != cfd || prev_fd != cfd)
        {
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%s\n%s\n", UPDATE_BACKUP, listenMap[cfd].c_str());
            send(prev_fd, buf, strlen(buf) + 1, 0); //通知前驱服务器开始备份
            usleep(1);
            LOG_DEBUG("notice %s Update Backup\n%s", prev_server_addr.c_str(), buf);
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%s\n%s\n", UPDATE_BACKUP, listenMap[next_fd].c_str(), buf);
            send(cfd, buf, strlen(buf) + 1, 0); //通知新注册的服务器开始备份
            LOG_DEBUG("notice %s Update Backup\n%s", newAddr.c_str(), buf);
        }
    }
    LOG_INFO("new cache_server regist : %s total %d", cache_server_str.c_str(), connectList.size());
    return 0;
}

int MasterServer::UpdateHash()
{
    //LOG_DEBUG("UpdateHash");
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    std::string msg;
    m_conn_lock.lock();
    for (auto iter : listenMap)
    {
        msg += iter.second;
        msg += "\n";
    }
    m_conn_lock.unlock();
    sprintf(buf, "%s\n%s", UPDATE_HASH, msg.c_str());
    
    for (auto iter : listenMap) 
    {
        int fd = iter.first;
        int len = send(fd, buf, strlen(buf) + 1, 0);
        if (len == -1) {
            LOG_WARN("send update_hash to %s failed, errno is %d", iter.second.c_str(), errno);
        }
        LOG_DEBUG("notice %s UpdateHash total %d len = %d\n%s", connectedMap[fd].c_str(), (int)listenMap.size(), len, buf);
    }
}

int MasterServer::startListen()
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
}

int MasterServer::EventLoop()
{
    epoll_event evs[1024];
    int ev_size = sizeof(evs) / sizeof(epoll_event);
    int len = sizeof(client_udp_addr);
    int ret;
    while (!shutdown) 
    {
        int num = epoll_wait(epfd, evs, ev_size, 0); //不阻塞
        if (num == -1)
        {
            LOG_ERROR("epoll waited failed");
            return -1;
        }
        for (int i = 0; i < num; i++) 
        {
            int cur_fd = evs[i].data.fd;
            if (cur_fd == master_client_sockfd) //处理client的请求
            {
                ret = dealwithClient();
                if (ret == -1) {
                    LOG_ERROR("dealwith client request failed");
                }
            }
            else if (cur_fd == lfd) //新的cache_server请求连接
            {
                ret = dealwithNewConn();
                if (ret == -1) {
                    LOG_ERROR("dealwith new connection request failed");
                }
            }
            else    //处理cache_server的消息
            {
                ret = dealwithCacheServer(cur_fd);
                if (ret == -1) {
                    LOG_ERROR("dealwith cache server request failed");
                }
            }
        }
    }
}

int MasterServer::dealwithNewConn()
{
    struct sockaddr_in client_addr;
    socklen_t client_addrlength = sizeof(client_addr);
    int cfd = accept(lfd, (struct sockaddr *)&client_addr, &client_addrlength);
    if (cfd < 0)
    {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        return -1;
    }
    std::string str_client_addr = sock_addr2str(client_addr);
    epollAddfd(cfd);
    m_conn_lock.lock();
    connectedMap.insert({cfd, str_client_addr});
    m_conn_lock.unlock();
    LOG_INFO("cache_server %s connected", str_client_addr.c_str());
    return 0;
}

int MasterServer::dealwithClient()
{
    char buf[1024];
    int len = sizeof(client_udp_addr);
    int rlen = recvfrom(master_client_sockfd, buf, sizeof(buf), 0, (sockaddr*)(&client_udp_addr), (socklen_t*)&len);
    std::string ip_port = sock_addr2str(client_udp_addr);
    std::string key = buf;
    bool ret = append(Request(GETDISTRIBUTE, client_udp_addr, key)); //将客户端请求加入请求队列
    if (ret ==false) 
    {
        LOG_ERROR("threadpool append failed on getdistribute request from %s with key %s", ip_port, buf);
        return -1;
    }
    return 0;
    //调试用，给客户端一个返回的信息
    /*char msg[1024];
    sprintf(msg, "I have received your msg : ");
    strcpy(msg, buf);
    sendto(master_client_sockfd, msg, strlen(msg) + 1, 0, (sockaddr*)&client_udp_addr, sizeof(client_udp_addr));*/
}

int MasterServer::dealwithCacheServer(int cfd)
{
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int len = recv(cfd, buf, sizeof(buf), 0);
    std::string str_client_addr = connectedMap[cfd];
    if (len < 0) {
        LOG_WARN("receive from cache_server %s failed, errno is  %d", str_client_addr.c_str(), errno);
        return -1;
    }
    else if (len == 0) {  //cache_server离线，容灾机制
        LOG_WARN("cache_server %s disconnected", str_client_addr.c_str());
        if (connectList.isExisted(str_client_addr)) {
            std::string prev_server_addr = connectList.getPrev(str_client_addr);
            std::string next_server_addr = connectList.getNext(str_client_addr);
            LOG_DEBUG("prev_server %s", prev_server_addr.c_str());
            LOG_DEBUG("next_server %s", next_server_addr.c_str());
            int prev_fd = registedMap[prev_server_addr];
            int next_fd = registedMap[next_server_addr];
            epollDelfd(cfd);
            close(cfd);
            m_conn_lock.lock();
            connectedMap.erase(cfd);
            registedMap.erase(str_client_addr);
            listenMap.erase(cfd);
            connectList.del(str_client_addr);
            m_conn_lock.unlock();
            //通知更新一致性哈希
            UpdateHash();
            usleep(1);
            if (next_fd == cfd || prev_fd == cfd)
                return 0;
            //通知恢复备份
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%s\n", RECOVER_BACKUP);
            send(next_fd, buf, strlen(buf), 0); //通知后继服务器将备份数据恢复
            usleep(1);
            LOG_DEBUG("notic recover backup %s", next_server_addr.c_str());
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%s\n%s\n", UPDATE_BACKUP, listenMap[next_fd].c_str());
            send(prev_fd, buf, strlen(buf), 0); //通知前驱服务器重新备份到后继服务器
            LOG_DEBUG("notic update backup %s", prev_server_addr.c_str());
        }
        else
            LOG_ERROR("cache_server %s not exist", str_client_addr.c_str());
    }
    else {
        sockaddr_in client_listen_addr = str2sock_addr(str_client_addr);
        client_listen_addr.sin_port = htons(atoi(buf));
        bool ret = append(Request(REGISTER, client_listen_addr, "", cfd)); //将客户端请求加入请求队列
        if (ret ==false) 
        {
            LOG_ERROR("threadpool append failed on register request from %s with listen port %s", str_client_addr, buf);
            return -1;
        }
    }
    return 0;
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
            Register(request.client_addr, request.cfd);
            break;
        }
        default:
            break;
        }
    }
}
//往工作队列中添加任务, 线程安全
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
