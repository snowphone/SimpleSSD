#include "ftl/hot_address_table.hh"

namespace SimpleSSD {

namespace FTL {

bool HotAddressTable::enabled = true;

HotAddressTable::HotAddressTable(uint64_t size) : capacity(size) {}

void HotAddressTable::update(uint64_t lpn) {
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

bool HotAddressTable::contains(uint64_t lpn) {
  return hotList.contains(lpn);
}

void HotAddressTable::setSize(uint64_t size) {
  this->capacity = size;
  shirinkToSize();
}

void HotAddressTable::shirinkToSize() {
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
}  // namespace FTL
}  // namespace SimpleSSD
