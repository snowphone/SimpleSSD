#include "ftl/prev_work/smt.hh"

#include <bits/stdint-uintn.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include "ftl/bad_page_table.hh"

namespace SimpleSSD {
namespace FTL {

std::map<uint32_t, uint32_t> SMT::createCounter() {
  std::map<uint32_t, uint32_t> counter;
  for (auto &p : bpt.table) {
    for (auto &i : p.second) {
      counter[p.first] += i.second;
    }
  }
  return counter;
}

SMT::SMT(const BadPageTable &bpt, uint32_t pageLen) : bpt(bpt), pageLen(pageLen) {
  auto counter = createCounter();
  auto backingIdx = counter.begin()->first;
  auto backingPageIdx = bpt.get(backingIdx, 0);
  for (auto badBlockIt = bpt.table.begin(); badBlockIt != bpt.table.end();
       ++badBlockIt) {
    auto blkIdx = badBlockIt->first;
    if (blkIdx == backingIdx)
      continue;
    for (auto jt = badBlockIt->second.begin(); jt != badBlockIt->second.end();
         ++jt) {
      for (uint32_t pageIdx = jt->first; pageIdx < jt->first + jt->second;
           ++pageIdx) {
        // Insert index
        smt[{blkIdx, pageIdx}] = {backingIdx, backingPageIdx};

        // Increment page index
        backingPageIdx += 1 + bpt.get(backingIdx, backingPageIdx + 1);

        // Get a new backing block if required
        if (backingPageIdx == pageLen) {
          backingIdx = next(badBlockIt, 1)->first;
          backingPageIdx = bpt.get(backingIdx, 0);
        }
      }
    }
  }
}

void SMT::allocatePage(uint32_t blkIdx, uint32_t pageIdx) {
  auto counter = createCounter();
  auto backingIdx = counter.begin()->first;
  auto& backingPageIdx = lastIndex[backingIdx];
  backingPageIdx = bpt.get(backingIdx, lastIndex[backingIdx]);


  smt[{blkIdx, pageIdx}] = {backingIdx, backingPageIdx};

  // Increment page index
  backingPageIdx += 1 + bpt.get(backingIdx, backingPageIdx + 1);
}

bool SMT::contains(uint32_t blkIdx, uint32_t pageIdx) {
  return get(blkIdx, pageIdx).has_value();
}

std::optional<Pair> SMT::get(uint32_t blkIdx, uint32_t pageIdx) {
  auto it = smt.find({blkIdx, pageIdx});
  if (it == smt.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool SMT::isBackingblock(uint32_t blkIdx) {
  for (auto &[_k, v] : this->smt) {
    auto [blk, _pg] = v;
    if (blk == blkIdx)
      return true;
  }
  return false;
}

}  // namespace FTL
}  // namespace SimpleSSD
