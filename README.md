# dcache
分布式存储系统

本系统为一个简单的分布式缓存系统，集群中包含一台Master及多台Cache Server。Master通过和Cache Server 之间的心跳检测获取集群中存活可用的Cache Server，构建数据在集群中的分布信息。Cache Server 负责数据的存储，并按照Master的指示完成数据的复制和迁移工作。Client 在启动的时候，从Master获取数据分布信息，根据数据分布信息，和相应的Cache server进行交互，完成用户的请求。

# master_server
功能实现：
1，通过一致性哈希管理cache_server
    cacher_server上线时向master_server注册自己，添加新节点以及虚拟节点
    当有新的cache_server上线或者离线时控制数据的复制、迁移
    定时向cache_server发送心跳包，检测是否有cache_server离线
2，向client提供的服务
    client向master_server查询key对应的value存储在哪台cache_server上
3，client与cache_server之间数据的增删查改
    通过udp向master_server查询数据分布，通过tcp传输数据
<<<<<<< HEAD

=======
>>>>>>> d3d1fc20bfd60b115c1b46dd4f448f1605b749ab
