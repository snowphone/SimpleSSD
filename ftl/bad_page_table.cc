#include "ftl/bad_page_table.hh"

namespace SimpleSSD {

namespace FTL {

void BadPageTable::insert(const uint32_t blkNo, const uint32_t pageNo) {
  auto &bpt = table[blkNo];

  int32_t prevPageNo;
  for (prevPageNo = pageNo - 1; prevPageNo >= 0; --prevPageNo) {
    if (bpt.count(prevPageNo))
      break;
  }

  // Sequential bad pages: [prevPageNo, prevPageNo + bpt[prevPageNo])
  if (prevPageNo >= 0 && prevPageNo + bpt[prevPageNo] == pageNo) {
    bpt[prevPageNo] += 1;
  }
  else {
    bpt[pageNo] = 1;
    prevPageNo = pageNo;
  }

  auto nextPageNo = prevPageNo + bpt[prevPageNo];
  if (bpt.count(nextPageNo)) {
    bpt[prevPageNo] += bpt[nextPageNo];
    bpt.erase(nextPageNo);
  }
}

uint32_t BadPageTable::count(const uint32_t blkNo) {
  uint32_t acc = 0;
  for (auto &[k, v] : table[blkNo]) {
    acc += v;
  }
  return acc;
}

uint32_t BadPageTable::get(const uint32_t blkNo, const uint32_t pageNo) {
  auto bbt_it = table.find(blkNo);
  if (bbt_it == table.end()) {
    return 0;
  }
  auto &bpt = bbt_it->second;
  auto it = bpt.find(pageNo);
  return it != bpt.end() ? it->second : 0;
}

}  // namespace FTL
}  // namespace SimpleSSD
