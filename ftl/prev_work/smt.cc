#include "ftl/prev_work//smt.hh"

#include <bits/stdint-uintn.h>

#include <iterator>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include "ftl/bad_page_table.hh"

namespace SimpleSSD {
namespace FTL {

SMT::SMT(const BadPageTable &bpt, uint32_t pageLen) : pageLen(pageLen) {
  std::map<uint32_t, uint32_t> counter;
  for (auto &p : bpt.table) {
    for (auto &i : p.second) {
      counter[p.first] += i.second;
    }
  }
  auto backingIdx = counter.begin()->first;
  auto backingPageIdx = bpt.get(backingIdx, 0);
  for (auto it = bpt.table.begin(); it != bpt.table.end(); ++it) {
    auto blkIdx = it->first;
    if (blkIdx == backingIdx)
      continue;
    for (auto jt = it->second.begin(); jt != it->second.end(); ++jt) {
      for (uint32_t pageIdx = jt->first; pageIdx < jt->first + jt->second;
           ++pageIdx) {
        // Insert index
        smt[{blkIdx, pageIdx}] = {backingIdx, backingPageIdx};

        // Increment page index
        backingPageIdx += 1 + bpt.get(backingIdx, backingPageIdx + 1);

        // Get a new backing block if required
        if (backingPageIdx == pageLen) {
          backingIdx = next(it, 1)->first;
          backingPageIdx = bpt.get(backingIdx, 0);
        }
      }
    }
  }
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

}  // namespace FTL
}  // namespace SimpleSSD
