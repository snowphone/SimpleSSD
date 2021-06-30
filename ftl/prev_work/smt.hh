#pragma once

#include <optional>
#include <unordered_map>
#include <map>
#include <utility>

#include "ftl/bad_page_table.hh"

using Pair = std::pair<uint32_t, uint32_t>;
namespace std {
template <>
struct hash<Pair> {
  size_t operator()(const Pair &p) const { return (p.first << 16) + p.second; }
};
}  // namespace std

namespace SimpleSSD {
namespace FTL {
class PageMapping;

class SMT {
  const BadPageTable& bpt;
  const PageMapping* mapping;
  uint32_t pageLen;
  std::unordered_map<Pair, Pair> smt;
  struct {
	  bool initialized = false;
	  uint32_t block,
			   page;
  } backingIndex;

  std::map<uint32_t, uint32_t> createCounter();
  void allocatePage(uint32_t blkIdx, uint32_t pageIdx);
  void moveBackingBlock(uint32_t blkIdx);

 public:
  SMT(const BadPageTable &bpt, uint32_t pageLen, PageMapping* mapping);
  bool contains(uint32_t blkIdx, uint32_t pageIdx);
  std::optional<Pair> get(uint32_t blkIdx, uint32_t pageIdx);
  std::optional<Pair> getAndAllocate(uint32_t blkIdx, uint32_t pageIdx);
  bool isBackingblock(uint32_t blkIdx);
};
}  // namespace FTL
}  // namespace SimpleSSD
