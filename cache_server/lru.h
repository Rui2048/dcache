#ifndef DCACHE_LRU_H_
#define DCACHE_LRU_H_

#include <unordered_map>
#include <iostream>
#include <functional>
#include <mutex> // for std::mutex
#include <vector>

//双链表节点
template<typename K, typename V>
class Node {
public:
    // typedef K Key;
    // typedef V Value;
public:
	Node() : key(K()), val(V()),dirty(false) {}
	Node(K k, V v):key(k), val(v) ,dirty(false){
		next = nullptr;
		prev = nullptr;
	}

public:
	K key;
    V val;
	bool dirty;
    Node<K, V>* next;
	Node<K, V>* prev;
};

template<typename Node>
class Doublelist {
public:
	Doublelist() {
		head = new Node();
		tail = new Node();
		head->next = tail;
		tail->prev = head;
		size_ = 0;
	}

	~Doublelist() {
		Node* curr = head->next;
		while(curr != tail) {
			Node* tmp = curr->next;
			delete curr;
			curr = tmp;
		}
		if (!head) delete head;
		if (!tail) delete tail;
	}

	//靠尾部的数据是最近使用的，靠头部的数据是最久为使用的
	//链表尾部添加节点x
	void addLast(Node* x) {
		x->prev = tail->prev;
		x->next = tail;

		tail->prev->next = x;
		tail->prev = x;
		size_++;
	}

	//删除节点(节点必须存在)
	void remove(Node* x) {
		x->prev->next = x->next;
		x->next->prev = x->prev;
		size_--;
	}

	//删除链表中第一个节点，并返回该节点，O(1)
	Node* removeFirst() {
		if (head->next == tail)
			return nullptr;
		Node* first = head->next;
		remove(first);
		return first;
	}

	//返回链表长度
	int size() {
		return size_;
	}
	
	void showlist() {
		Node* travel = head->next;
		std::cout << "list: ";
		while (travel != tail) {
			std::cout << travel->key << ":" << travel->val <<":" << (travel->dirty? "true" : "false") << "-->";
			travel = travel->next;
		}
		std::cout << std::endl;
	}

	std::vector<Node*> lru_node_oneshot()
	{
		std::vector<Node*> v;
		Node* travel = head->next;
		std::cout << "list: ";
		while (travel != tail) {
			v.push_back(travel);
			travel = travel->next;
		}
		return v;
	}
public:
	//头尾虚节点
	Node* head;
	Node* tail;
	//链表元素数
	int size_;

	// friend class ThreadSafeLRUCache<std::string,std::string>;
};

template<typename Key, typename Value>
class LRUCache
{
public:
	explicit LRUCache(int capacity): cap(capacity) {
		cache = new Doublelist<LruNode>();
	}

	~LRUCache() {
		if (!cache) delete cache;
	}

	void set(Key key, Value val) {
		//dcache::DoneGuard done(std::bind(&CacheList::showlist, cache)); // debug
		if (map.find(key) != map.end()) {
			//删除旧数据
			deleteKey(key);
			//新插入的数据为最近使用的数据
			addRecently(key, val);
			return;
		}

		if (cap == cache->size()) {
			//删除最久未使用的元素
			removeLeastRecently();
		}

		//添加为最近使用的元素
		addRecently(key, val);
	}

	// 调用此函数前先检查hasKey!!!
	Value get(Key key) {
		//dcache::DoneGuard done(std::bind(&CacheList::showlist, cache)); // debug
		makeRecently(key); //将该数据提升为最近使用的
		return map[key]->val;
	}

	bool hasKey(Key key) const {
		return map.find(key) != map.end();
	}

private:
	//将key提升为最近使用的
	void makeRecently(Key key) {
		LruNode* x = map[key];
		cache->remove(x);
		cache->addLast(x);
	}

	//添加最近使用的元素
	void addRecently(Key key, Value val) {
		LruNode* x = new LruNode(key, val);
		cache->addLast(x);
		map.insert(std::make_pair(key, x));
	}

	//删除某个key
	void deleteKey(Key key) {
		LruNode* x = map[key];
		cache->remove(x);
		map.erase(key);
		delete x;
	}

	//删除最久未使用的元素
	void removeLeastRecently() {
		LruNode* deletedNode = cache->removeFirst();
		map.erase(deletedNode->key);
		delete deletedNode;
	}

private:
	typedef Node<Key, Value> LruNode;
	typedef Doublelist<LruNode> CacheList;

	std::unordered_map<Key, LruNode*> map;
	CacheList* cache;
	int cap;  //最大容量
};

template<typename Key, typename Value>
class ThreadSafeLRUCache
{
public:
	explicit ThreadSafeLRUCache(int capacity): cap(capacity) {
		cache = new Doublelist<LruNode>();
	}

	~ThreadSafeLRUCache() {
		if (!cache) delete cache;
	}

	void set(Key key, Value val) {
		std::lock_guard<std::mutex> lck(m_mutex);
		//dcache::DoneGuard done(std::bind(&CacheList::showlist, cache)); // debug
		if (map.find(key) != map.end()) {
			//删除旧数据
			deleteKey(key);
			//新插入的数据为最近使用的数据
			addRecently(key, val);
			return;
		}

		if (cap == cache->size()) {
			//删除最久未使用的元素
			removeLeastRecently();
		}

		//添加为最近使用的元素
		addRecently(key, val);
	}

	// 调用此函数前先检查hasKey!!!
	Value get(const Key& key) {
		std::lock_guard<std::mutex> lck(m_mutex);
		//dcache::DoneGuard done(std::bind(&CacheList::showlist, cache)); // debug
		makeRecently(key); //将该数据提升为最近使用的
		return map[key]->val;
	}

	bool hasKey(const Key& key) const {
		return map.find(key) != map.end();
	}

	std::vector<Key> lru_keys_oneshot()
	{
		std::lock_guard<std::mutex> lck(m_mutex);
		std::vector<Key> v;
		LruNode* travel = cache->head->next;
		while (travel != cache->tail) {
			v.push_back(travel->key);
			travel = travel->next;
		}
		return v;
	}

	void set_dirty(Key key)
	{
		std::lock_guard<std::mutex> lck(m_mutex);
		if(map.find(key) != map.end())
		{
			map[key]->dirty = true;
		}
	}

	void set_clean(Key key)
	{
		std::lock_guard<std::mutex> lck(m_mutex);
		if(map.find(key) != map.end())
		{
			map[key]->dirty = false;
		}
	}

	bool is_dirty(Key key)
	{
		std::lock_guard<std::mutex> lck(m_mutex);
		if(map.find(key) != map.end())
		{
			return (map[key]->dirty == true);
		}
		return false;
	}

private:
	//将key提升为最近使用的
	void makeRecently(Key key) {
		LruNode* x = map[key];
		cache->remove(x);
		cache->addLast(x);
	}

	//添加最近使用的元素
	void addRecently(Key key, Value val) {
		LruNode* x = new LruNode(key, val);
		cache->addLast(x);
		map.insert(std::make_pair(key, x));
	}

	//删除某个key
	void deleteKey(Key key) {
		LruNode* x = map[key];
		cache->remove(x);
		map.erase(key);
		delete x;
	}

	//删除最久未使用的元素
	void removeLeastRecently() {
		LruNode* deletedNode = cache->removeFirst();
		map.erase(deletedNode->key);
		delete deletedNode;
	}
	
	

private:
	typedef Node<Key, Value> LruNode;
	typedef Doublelist<LruNode> CacheList;

	std::unordered_map<Key, LruNode*> map;
	CacheList* cache;
	int cap;  //最大容量
	std::mutex  m_mutex; //互斥锁
};

#endif // DCACHE_LRU_H_