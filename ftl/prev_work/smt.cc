#include "ftl/prev_work/smt.hh"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include "ftl/bad_page_table.hh"
#include "ftl/page_mapping.hh"

namespace SimpleSSD {
namespace FTL {

#define TODO(MSG) throw runtime_error(MSG)

SMT::SMT(const BadPageTable &bpt, uint32_t pageLen, PageMapping *mapping)
    : bpt(bpt), mapping(mapping), pageLen(pageLen) {
  for (auto badBlockIt = bpt.table.begin(); badBlockIt != bpt.table.end();
       ++badBlockIt) {
    auto blkIdx = badBlockIt->first;
    for (auto jt = badBlockIt->second.begin(); jt != badBlockIt->second.end();
         ++jt) {
      for (uint32_t pageIdx = jt->first; pageIdx < jt->first + jt->second;
           ++pageIdx) {
        // Insert index
        allocatePage(blkIdx, pageIdx);
      }
    }
  }
}

std::map<uint32_t, uint32_t> SMT::createCounter() {
  std::map<uint32_t, uint32_t> counter;
  for (auto &p : bpt.table) {
    for (auto &i : p.second) {
      counter[p.first] += i.second;
    }
  }
  return counter;
}

/**
 * if no backing block or backing block is full
 *   -> allocate new backing block and move it from freeblocks to blocks
 *
 * if blkIdx == backingBlockIndex -> do nothing
 * else
 *   Using backing block and index variable, update map.
 */
void SMT::allocatePage(uint32_t blkIdx, uint32_t pageIdx) {
  // If backing block not exists or is full
  if (!backingIndex.initialized ||
      backingIndex.page >= mapping->param.pagesInBlock) {
    backingIndex.initialized = true;

    auto counter = createCounter();
    backingIndex.block = counter.begin()->first;
    backingIndex.page = bpt.get(backingIndex.block, 0);
	debugprint(LOG_FTL_PAGE_MAPPING, "Allocate a new backing block %lu", backingIndex.block);

    moveBackingBlock(backingIndex.block);
  }

  if (blkIdx == backingIndex.block) {
    return;
  }

  smt[{blkIdx, pageIdx}] = {backingIndex.block, backingIndex.page};

  // Increment page index
  backingIndex.page += 1 + bpt.get(backingIndex.block, backingIndex.page + 1);
}

void SMT::moveBackingBlock(uint32_t blkIdx) {
  auto &cold = mapping->cold;
  auto it = find_if(cold.freeBlocks.begin(), cold.freeBlocks.end(),
                    [blkIdx](Block &b) { return b.getBlockIndex() == blkIdx; });
  if (it == cold.freeBlocks.end())
    return;

  cold.blocks.emplace(it->getBlockIndex(), move(*it));
  cold.freeBlocks.erase(it);
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

std::optional<Pair> SMT::getAndAllocate(uint32_t blkIdx, uint32_t pageIdx) {
  auto results = this->get(blkIdx, pageIdx);
  if (results.has_value())
    allocatePage(blkIdx, pageIdx);

  return results;
}

}  // namespace FTL
}  // namespace SimpleSSD
