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
class SMT {
  const BadPageTable& bpt;
  uint32_t pageLen;
  std::unordered_map<Pair, Pair> smt;
  std::unordered_map<uint32_t, uint32_t> lastIndex;

  std::map<uint32_t, uint32_t> createCounter();
  void allocatePage(uint32_t blkIdx, uint32_t pageIdx);

 public:
  SMT(const BadPageTable &bpt, uint32_t pageLen);
  bool contains(uint32_t blkIdx, uint32_t pageIdx);
  std::optional<Pair> get(uint32_t blkIdx, uint32_t pageIdx);
  bool isBackingblock(uint32_t blkIdx);
};
}  // namespace FTL
}  // namespace SimpleSSD
