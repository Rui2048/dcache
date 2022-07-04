#ifndef SERVERLIST_H
#define SERVERLIST_H

#include <string>
#include <unistd.h>
#include <unordered_map>

template <typename T>
struct Node
{
    T server_ID;
    Node<T> *next;
    Node<T> *prev;
    Node() : server_ID(T()), next(nullptr), prev(nullptr) {}
    Node(T t) : server_ID(t), next(nullptr), prev(nullptr) {}
};

template <typename T>
class DLinkList
{
public:
    DLinkList ()
    {
        size_ = 0;
        head = new Node<T>;
        tail = new Node<T>;
        head->next = tail;
        tail->prev = head;
    }
    ~DLinkList()
    {
        Node<T> *node = head->next;
        while (node != tail) {
            Node<T> *temp = node->next;
            delete node;
            node = temp;
        }
        delete head;
        delete tail;
    }
    void addLast(Node<T> *node)
    {
        tail->prev->next = node;
        node->prev = tail->prev;
        node->next = tail;
        tail->prev = node;
        size_++;
    }
    void delNode(Node<T> *node)
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        delete node;
        size_--;
    }
    int size() {return size_;}

    Node<T> *getHead() {return head;}
    Node<T> *getTail() {return tail;}
    bool isHead(Node<T> *node) {return node == head;}
    bool isTail(Node<T> *node) {return node == tail;}

private:
    int size_;
    Node<T> *head;
    Node<T> *tail;

};

template <typename T>
class ServerList
{
public:
    ServerList() : list(new serverDlink){}
    ~ServerList() {delete list;}
    bool isExisted(T t)
    {
        return map_.count(t) > 0;
    }

    int size() {return list->size();}
    
    //add之前一定要检查isExisted
    void add(T t) 
    {
        serverNode *node = new serverNode(t);
        list->addLast(node);
        map_[t] = node;
    }

    //del之前一定要检查isExisted
    void del(T t)
    {
        serverNode *node = map_[t];
        list->delNode(node);
        map_.erase(t);
    }

    //getNext之前一定要检查isExisted
    T getNext(T t)
    {
        serverNode *node = map_[t];
        node = node->next;
        return list->isTail(node) ? list->getHead()->next->server_ID : node->server_ID;
    }

    //getPrev之前一定要检查isExisted
    T getPrev(T t)
    {
        serverNode *node = map_[t];
        node = node->prev;
        return list->isHead(node) ? list->getTail()->prev->server_ID : node->server_ID;
    }

private:
    typedef Node<T> serverNode;
    typedef DLinkList<T> serverDlink;
    std::unordered_map<T, serverNode*> map_;
    serverDlink *list;
};

#endif  //end of SERVERLIST_H