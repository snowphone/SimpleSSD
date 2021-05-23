#pragma once

#include <string>
#include <unordered_map>

namespace SimpleSSD {

namespace FTL {

class BadPageTable {
  friend class SMT;
  // Block#, Page #, sequential bad page #
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> table;

 public:
  /**
   * Insert a bad page into the table and merge if there's sequential bad pages
   * in a block. Estimated complexity is O(n) where n is the total number of
   * pages in a block.
   */
  void insert(const uint32_t blkNo, const uint32_t pageNo);

  /**
   * Returns the total number of bad pages in a block at O(n).
   */
  uint32_t count(const uint32_t blkNo) const;

  /**
   * Returns the number of sequential bad pages at O(1).
   */
  uint32_t get(const uint32_t blkNo, const uint32_t pageNo) const;

  /**
   * Not for simulation per se, but for debugging and information
   */
  std::string to_string() const;
};

}  // namespace FTL
}  // namespace SimpleSSD
