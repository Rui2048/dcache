#include "cache_service.h"
#include <assert.h>
#include <iostream>
#include <string.h>

CacheServer::CacheServer() : 
lru(lru_size), lru_backup(lru_size),
cacheServerHash(new con_hash)
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
    std::string logName = "log_";
    logName += port2str(cache_server_port);
    Log::get_instance()->init(logName.c_str(), m_close_log, 8000, 50000, 0);
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
    local_addr = sock_addr2str(cache_server_sockaddr);
    return 0;
}

int CacheServer::eventLoop()
{
    epoll_event evs[1024];
    int ev_size = sizeof(evs) / sizeof(epoll_event);
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
                if (dealwithMasterMsg() == -1)
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

int CacheServer::getValue(std::string key, int cfd, std::string client_addr)
{
    std::string msg;
    std::string value;
    if (lru.hasKey(key) && lru.is_dirty(key)) { //脏数据
        msg += DIRTY_DATA;
    }
    else {
        if (lru.hasKey(key)) {
            msg += GET_SUCCESS;
            msg += "\n";
            value = lru.get(key);
            msg += value;
        }
        else {
            msg += DATA_NOT_EXIST;
        }
    }
    char buf[1024];
    strcpy(buf, msg.c_str());
    int len = send(cfd, buf, strlen(buf), 0);
    if (len == -1) {
        return -1;
    }
    LOG_INFO("Get:  getValue success %s, key %s value %s", client_addr.c_str(), key.c_str(), value.c_str());
    return 0;
}

int CacheServer::setValue(std::string key, std::string value, std::string client_addr)
{
    lru_mutex.lock();
    if (lru.hasKey(key) && lru.is_dirty(key)) {
        LOG_INFO("Set:  find key %s is dirty", client_addr.c_str(), key.c_str());
    }
    /*else if (lru.hasKey(key) && cacheServerHash != nullptr && cacheServerHash->conhash_get_CacheServer(key) != local_addr) {
        LOG_INFO("Set:  find key %s is dirty", client_addr.c_str(), key.c_str());
    }*/
    else {
        lru.set(key, value);
        LOG_INFO("Set:  set key %s success value %s", key.c_str(), value.c_str());
    }
    lru_mutex.unlock();
    return 0;
}

int CacheServer::getBackupValue(std::string key, int cfd, std::string client_addr)
{
    std::string msg;
    if (lru_backup.hasKey(key) && lru_backup.is_dirty(key)) { //脏数据
        msg += DIRTY_DATA;
    }
    else {
        if (lru_backup.hasKey(key)) {
            msg += GET_SUCCESS;
            msg += "\n";
            msg += lru_backup.get(key);
        }
        else {
            msg += DATA_NOT_EXIST;
        }
    }
    char buf[1024];
    strcpy(buf, msg.c_str());
    int len = send(cfd, buf, strlen(buf), 0);
    if (len == -1) {
        return -1;
    }
    return 0;
}

int CacheServer::setBackupValue(std::string key, std::string value, std::string client_addr)
{
    lru_backup_mutex.lock();
    if (lru_backup.hasKey(key) && lru_backup.is_dirty(key)) {
        LOG_INFO("Set:  find key %s is dirty", client_addr.c_str(), key.c_str());
    }
    /*else if (lru.hasKey(key) && cacheServerHash != nullptr && cacheServerHash->conhash_get_CacheServer(key) != local_addr) {
        LOG_INFO("Set:  find key %s is dirty", client_addr.c_str(), key.c_str());
    }*/
    else {
        lru_backup.set(key, value);
        LOG_INFO("Set:  set key %s success value %s", key.c_str(), value.c_str());
    }
    lru_backup_mutex.unlock();
    return 0;
}

int CacheServer::updateHash(std::vector<std::string> ip_ports)
{
    LOG_INFO("start updateHash total:%d", (int)ip_ports.size());
    hash_mutex.lock();
    if (cacheServerHash != nullptr) {
        delete cacheServerHash;
    }
    cacheServerHash = new con_hash;
    for (std::string ip_port : ip_ports) {
        cacheServerHash->conhash_add_CacheServer(ip_port);
        LOG_INFO("cache_server conhash add ip_port %s", ip_port.c_str());
    }
    hash_mutex.unlock();
    LOG_INFO("finish updateHash total:%d\n", (int)ip_ports.size());
    return 0;
}

void *CacheServer::updateBackup(CacheServer *server, std::string ip_port)
{
    server->update(ip_port);
}

void CacheServer::update(std::string ip_port)
{
    LOG_INFO("start update backup to %s", ip_port.c_str());
    sockaddr_in dst_addr = str2sock_addr(ip_port);
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    int ret = connect(cfd, (sockaddr*)(&dst_addr), sizeof(dst_addr));
    if (ret == -1)
    {
        LOG_ERROR("connect to backup cache_server failed %s , errno is %d", ip_port.c_str(), errno);
        return;
    }
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%s", BACKUP_INVALID);
    send(cfd, buf, strlen(buf), 0); //通知备份服务器原备份失效，准备接收新的备份
    
    std::vector<std::string> keys = lru.lru_keys_oneshot();
    for (auto key : keys) 
    {
        memset(buf, 0, sizeof(buf));
        sprintf(buf, "%s\n%s\n%s", SET_BACKUP_VALUE, key.c_str(), lru.get(key).c_str());
        send(cfd, buf, strlen(buf), 0); //备份
    }
    LOG_INFO("update backup finish\n");
}

int CacheServer::recoverBackup()
{
    state = 1;
    std::thread t(move_worker, this);
    t.detach();
}

void *CacheServer::move_worker(CacheServer *server)
{   
    server->move();
}

void CacheServer::move()
{
    std::vector<std::string> keys;
    if (state == 0) {
        LOG_INFO("start move data");
        keys = lru.lru_keys_oneshot();
        lru_mutex.lock();
    }
    else {
        LOG_INFO("start recover backup");
        keys = lru_backup.lru_keys_oneshot();
        lru_backup_mutex.lock();
    }
    for(auto key : keys)
        {
            int cfd = socket(AF_INET, SOCK_STREAM, 0); //用于cache_server之间交换数据
            std::string cur_addr;
            //依次将key标记为dirty
            if(lru.hasKey(key))
            {
                LOG_INFO("Move:  key still in local lru : %s", key.c_str());
                std::string value = lru.get(key);
                //计算新的ip地址
                std::string newAddr = cacheServerHash->conhash_get_CacheServer(key);
                //如果需要迁移
                if(newAddr != local_addr)
                {
                    LOG_INFO( "Move:  key %s has to move to new address : %s", key.c_str(), newAddr.c_str());
                    if (newAddr != cur_addr) {
                        close(cfd);
                        sockaddr_in addr = str2sock_addr(newAddr);
                        int ret = connect(cfd, (sockaddr*)(&addr), sizeof(addr));
                        if (ret == -1) {
                            LOG_ERROR( "Move:  connect to cache_server : %s failed, errno is %d", newAddr.c_str(), errno);
                            break;
                        }
                        cur_addr = newAddr;
                    }
                    std::string msg;
                    msg += SET_VALUE;
                    msg += '\n';
                    msg += key;
                    char buf[1024];
                    strcpy(buf, msg.c_str());
                    int ret = send(cfd, buf, strlen(buf), 0);
                    if (ret == -1) {
                        LOG_ERROR( "Move:  send move msg to cache_server : %s failed, errno is %d", newAddr.c_str(), errno);
                        break;
                    }
                    lru.set_dirty(key);
                }
                else
                {
                    LOG_INFO("Move:  key %s do not has to move", key.c_str())
                }
                    
            }
        }
        if (state == 0) {
            LOG_INFO("move data finish\n");
            lru_mutex.unlock();
        }
        else {
            LOG_INFO("recover backup finish\n");
            lru_backup_mutex.unlock();
            state = 0;
            backup_invalid = 1; //原备份已失效
        }
}

int CacheServer::dealwithMasterMsg()
{
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int len = recv(master_sockfd, buf, sizeof(buf), 0);
    if (len == 0) 
    {
        LOG_ERROR("master_server is crashed !!!");
        epollDelfd(master_sockfd);
        return -1;
    }
    else if (len == -1) 
    {
        LOG_ERROR("recv from master_server failed, errno is %d", errno);
        return -1;
    }
    //LOG_DEBUG("recv from master: len = %d\n%s", len, buf);
    std::string temp;
    int i = 0;
    while (buf[i] != '\n') {
        temp += buf[i++];
    }

    //LOG_DEBUG("recv from master: len = %d\n%s", (int)temp.size(), temp.c_str());

    if (temp == MOVE_DATA)
    {
        LOG_DEBUG("MOVE_DATA");
        std::thread t(move_worker, this);
        t.detach();
    }
    else if (temp == UPDATE_HASH)
    {
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
            i++;
        }
        updateHash(ip_ports);
    }
    else if (temp == UPDATE_BACKUP)
    {
        i++;
        temp = "";
        while (buf[i] != '\n') {
            temp += buf[i];
            i++;
        }
        thread t(updateBackup, this, temp);
        t.detach();
    }
    else if (temp == RECOVER_BACKUP)
    {
        recoverBackup();
    }  
    
}

int CacheServer::dealwithClientMsg(int cfd)
{
    char buf[1024];
    int len = recv(cfd, buf, sizeof(buf), 0);
    if (len == 0) 
    {
        LOG_INFO("tcp disconnected %s", connectedMap[cfd].c_str());
        sockfd_mutex.lock();
        connectedMap.erase(cfd);
        sockfd_mutex.unlock();
        epollDelfd(cfd);
        return -1;
    }
    else if (len == -1) 
    {
        LOG_ERROR("recv from master_server failed, errno is %d", errno);
        return -1;
    }
    std::string opt, key, value;
    int i = 0;
    while (buf[i] != '\n') {
        opt += buf[i++];
    }
    if (opt == BACKUP_INVALID) { //原备份失效
        std::vector<std::string> keys = lru_backup.lru_keys_oneshot();
        
    }
    else { //set value
        i++;
        while (i < len && buf[i] != '\n') {
            key += buf[i];
            i++;
        }
        if (opt == SET_VALUE || opt == SET_BACKUP_VALUE)
        while (i < len && buf[i] != '\n') {
            value += buf[i];
            i++;
        }
        Request request(opt, connectedMap[cfd], key, value, cfd);
        bool ret = append(request); //将客户端请求加入请求队列
        if (ret ==false) 
        {
            LOG_ERROR("threadpool append failed on request from %s", connectedMap[cfd]);
            return -1;
        } 
    }

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
    std::string ip_port = sock_addr2str(addr);
    sockfd_mutex.lock();
    //sockfd_set.insert(cfd);
    connectedMap.insert({cfd, ip_port});
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
        std::string opt = request.request_type;
        std::string key = request.key;
        std::string value = request.value;
        std::string client_addr = request.client_addr;
        int cfd = request.cfd;
        int ret;
        if (opt == SET_VALUE) {
            ret = setValue(key, value, client_addr);
            if (ret == -1)
                LOG_ERROR("Set:  %s setValue failed on key %s", client_addr.c_str(), key.c_str());
        }
        else if (opt == GET_VALUE) {
            ret = getValue(key, cfd, client_addr);
            if (ret == -1)
                LOG_ERROR("Get:  %s getValue failed on key %s", client_addr.c_str(), key.c_str());
        }
        else if (opt == SET_BACKUP_VALUE) {
            ret = setBackupValue(key, value, client_addr);
            if (ret == -1)
                LOG_ERROR("Set:  %s setBackupValue failed on key %s", client_addr.c_str(), key.c_str());
        }
        else if (opt == GET_BACKUP_VALUE) {
            ret = getBackupValue(key, cfd, client_addr);
            if (ret == -1)
                LOG_ERROR("Get:  %s getBackupValue failed on key %s", client_addr.c_str(), key.c_str());
        }
        else {
            LOG_ERROR("uknown operation from %s", connectedMap[cfd].c_str());
        }
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
