#include "./conhash/conhash.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace dcache
{
  class con_hash
  {
  public:
    con_hash()
    {
      conhash = conhash_init(NULL);
    }

    ~con_hash()
    {

      conhash_fini(conhash);
    }

    //添加cache server
    void conhash_add_CacheServer(std::string ip_port)
    {

      std::shared_ptr<struct node_s> node = std::make_shared<struct node_s>();
      if (node)
      {

        conhash_set_node(node.get(), ip_port.c_str(), 32);
        {
          std::lock_guard<std::mutex> lk(node_mutex);
          conhash_add_node(conhash, node.get());
          nodes_map[ip_port] = node;
        }
      }
    }

    //删除cache server
    void conhash_del_CacheServer(std::string ip_port)
    {

      if (nodes_map.count(ip_port))
      {

        std::lock_guard<std::mutex> lk(node_mutex);

        std::shared_ptr<struct node_s> node = nodes_map[ip_port];
        conhash_del_node(conhash, node.get());

        nodes_map.erase(ip_port);
      }
    }

    //根据key得到cache server的ip+端口
    std::string conhash_get_CacheServer(std::string key)
    {

      const struct node_s *node = conhash_lookup(conhash, key.c_str());
      if (node)
      {
        return node->iden;
      }
      else
      {
        return "NULL";
      }
    }

    //根据key得到备份cache server的ip+端口
    std::string conhash_get_CacheServer2(std::string key)
    {

      const struct node_s *node = conhash_lookup2(conhash, key.c_str());
      if (node)
      {
        return node->iden;
      }
      else
      {
        return "NULL";
      }
    }

  private:
    struct conhash_s *conhash;
    std::unordered_map<std::string, std::shared_ptr<struct node_s>> nodes_map;

    std::mutex node_mutex;
  };
}
