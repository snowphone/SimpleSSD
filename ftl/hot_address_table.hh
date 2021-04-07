#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

#include "ftl/LRU.hh"

namespace SimpleSSD {

namespace FTL {

class HotAddressTable {
  LRU<uint64_t> hotList;
  LRU<uint64_t> candidateList;
  uint64_t capacity;

 public:
  static bool enabled;

  HotAddressTable(uint64_t size = 0);

  void update(uint64_t lpn);

  bool contains(uint64_t lpn);

  void setSize(uint64_t size);

 private:
  void shirinkToSize();
};

}  // namespace FTL
}  // namespace SimpleSSD
