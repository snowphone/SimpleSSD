#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

#include "ftl/LRU.hh"

namespace SimpleSSD {

namespace FTL {

class HotList {
  LRU<uint64_t> hotList;
  LRU<uint64_t> candidateList;
  uint64_t capacity;

 public:
  static bool enabled;

  HotList(uint64_t size = 0) : capacity(size) {}

  void update(uint64_t lpn) {
    if (hotList.contains(lpn)) {
      hotList.update(lpn);
    }
    else if (candidateList.contains(lpn)) {
      candidateList.erase(lpn);
      hotList.insert(lpn);
    }
    else {
      candidateList.insert(lpn);
    }

    shirinkToSize();
  }

  bool contains(uint64_t lpn) { return hotList.contains(lpn); }

  void setSize(uint64_t size) {
    this->capacity = size;
    shirinkToSize();
  }

 private:
  void shirinkToSize() {
    // If a hot list is not full and candidate entries exist
    while (hotList.size() < capacity && candidateList.size() > 0) {
      // Move candidate items to the hot list.
      uint64_t entry = *candidateList.begin();
      candidateList.pop_front();
      hotList.insert(entry);
    }
    while (hotList.size() > capacity) {
      // Move hot's LRU to candidate's MRU
      uint64_t entry = *hotList.rbegin();
      hotList.pop_back();
      candidateList.insert(entry);
    }
    while (candidateList.size() > capacity) {
      // Evict LRU
      candidateList.pop_back();
    }
  }
};

}  // namespace FTL
}  // namespace SimpleSSD