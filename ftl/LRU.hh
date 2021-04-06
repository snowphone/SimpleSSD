#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

namespace SimpleSSD {

namespace FTL {

template <typename T = uint64_t>
class LRU {
  std::unordered_map<T, typename std::list<T>::iterator> hashTable;
  std::list<T> list;

 public:
  void insert(const T &addr) {
    list.push_front(addr);
    hashTable[addr] = list.begin();
  }

  void erase(const T &addr) {
    auto it = hashTable[addr];
    list.erase(it);
    hashTable.erase(addr);
  }

  void update(const T &addr) {
    erase(addr);
    insert(addr);
  }

  typename std::list<T>::iterator find(const T &addr) {
    auto it = hashTable.find(addr);
    if (it != hashTable.end())
      return it->second;
    else
      return list.end();
  }

  bool contains(const T &addr) { return find(addr) != list.end(); }

  uint64_t size() { return hashTable.size(); }

  void pop_back() {
    hashTable.erase(list.back());
    list.pop_back();
  }

  void pop_front() {
    hashTable.erase(list.front());
    list.pop_front();
  }

  typename std::list<T>::iterator begin() { return list.begin(); }

  typename std::list<T>::iterator end() { return list.end(); }

  typename std::list<T>::reverse_iterator rbegin() { return list.rbegin(); }

  typename std::list<T>::reverse_iterator rend() { return list.rend(); }
};

}  // namespace FTL
}  // namespace SimpleSSD
