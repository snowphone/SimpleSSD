/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ftl/page_mapping.hh"

#include <algorithm>
#include <iterator>
#include <limits>
#include <random>
#include <string>

#include "util/algorithm.hh"
#include "util/bitset.hh"

namespace SimpleSSD {

namespace FTL {

PageMapping::PageMapping(ConfigReader &c, Parameter &p, PAL::PAL *l,
                         DRAM::AbstractDRAM *d)
    : AbstractFTL(p, l, d),
      pPAL(l),
      conf(c),
      bReclaimMore(false),
      validPageAcc(0),
      validPageCnt(0) {
  for (auto &m : blkClusters) {
    m.lastFreeBlock = vector<uint32_t>(param.pageCountToMaxPerf);
    m.lastFreeBlockIOMap = Bitset(param.ioUnitInPage);
  }

  cold.blocks.reserve(param.totalPhysicalBlocks);
  table.reserve(param.totalLogicalBlocks * param.pagesInBlock);

  salvationConfig.enabled =
      conf.readBoolean(CONFIG_FTL, FTL_USE_BAD_BLOCK_SALVATION);
  salvationConfig.unavailablePageThreshold =
      conf.readDouble(CONFIG_FTL, FTL_UNAVAILABLE_PAGE_THRESHOLD);
  double ber = conf.readDouble(CONFIG_FTL, FTL_BER);
  double sigma = conf.readDouble(CONFIG_FTL, FTL_SIGMA);
  salvationConfig.setModel(make_unique<LogNormal>(ber, sigma, param.pageSize));
  HotAddressTable::enabled = salvationConfig.enabled &&
                             conf.readBoolean(CONFIG_FTL, FTL_ENABLE_HOT_COLD);

  debugprint(LOG_FTL_PAGE_MAPPING, "%s", salvationConfig.to_string().c_str());

  for (uint32_t i = 0; i < param.totalPhysicalBlocks; i++) {
    auto blk =
        Block(i, param.pagesInBlock, param.ioUnitInPage, salvationConfig);

    if (salvationConfig.enabled) {
      if (HotAddressTable::enabled) {
        if (!blk.getUnavailablePageCount()) {
          cold.freeBlocks.emplace_back(std::move(blk));
        }
        else if (blk.getUnavailablePageRatio() <
                 salvationConfig.unavailablePageThreshold) {
          hot.freeBlocks.emplace_back(std::move(blk));
        }
        else {
          // Drop the block
        }
      }
      else {
        if (blk.getUnavailablePageRatio() <
            salvationConfig.unavailablePageThreshold) {
          cold.freeBlocks.emplace_back(std::move(blk));
        }
        else {
          // Drop the block
        }
      }
    }
    else {
      if (blk.getUnavailablePageCount() == 0)
        cold.freeBlocks.emplace_back(std::move(blk));
    }
  }

  float hotAddressTableSizeRatio =
      conf.readDouble(CONFIG_FTL, FTL_HOT_COLD_CAPACITY_RATIO);
  uint64_t hotAddressTableSize = hot.freeBlocks.size() * param.pagesInBlock;
  for (auto &b : hot.freeBlocks) {
    hotAddressTableSize -= b.getUnavailablePageCount();
  }
  hotAddressTableSize *= hotAddressTableSizeRatio;

  salvationConfig.hotAddressTable.setSize(hotAddressTableSize);

  debugprint(LOG_FTL_PAGE_MAPPING, "Hot address table-hot block ratio: %f",
             hotAddressTableSizeRatio);
  debugprint(LOG_FTL_PAGE_MAPPING, "Trace %" PRIu64 " hot LPNs",
             hotAddressTableSize);

  uint64_t nTotalPhysicalPages = 0;
  for (auto &m : blkClusters) {
    auto &blks = m.freeBlocks;
    for (auto &b : blks) {
      nTotalPhysicalPages += param.pagesInBlock - b.getUnavailablePageCount();
    }
  }

  debugprint(LOG_FTL_PAGE_MAPPING, "Designed physical pages: %" PRIu64,
             param.totalPhysicalBlocks * param.pagesInBlock);
  debugprint(LOG_FTL_PAGE_MAPPING, "Total physical pages: %" PRIu64,
             nTotalPhysicalPages);

  debugprint(LOG_FTL_PAGE_MAPPING,
             "Logical free blocks: %lu, actual free blocks: %lu",
             param.totalPhysicalBlocks,
             cold.freeBlocks.size() + hot.freeBlocks.size());
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Hot free blocks: %lu, cold free blocks: %lu",
             hot.freeBlocks.size(), cold.freeBlocks.size());

  status.totalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;

  // Allocate free blocks
  for (uint32_t i = 0; i < param.pageCountToMaxPerf; i++) {
    for (auto &m : blkClusters) {
      if (&m == &cold || HotAddressTable::enabled) {
        m.lastFreeBlock.at(i) = _getFreeBlock(i, m);
      }
    }
  }

  for (auto &m : blkClusters) {
    m.lastFreeBlockIndex = 0;
  }

  memset(&stat, 0, sizeof(stat));

  // mjo: RandomTweak is only used when Superpaging is enabled.
  // So, it's not my business :)
  bRandomTweak = conf.readBoolean(CONFIG_FTL, FTL_USE_RANDOM_IO_TWEAK);
  bitsetSize = bRandomTweak ? param.ioUnitInPage : 1;
}

PageMapping::~PageMapping() {}

bool PageMapping::initialize() {
  uint64_t nPagesToWarmup;
  uint64_t nPagesToInvalidate;
  uint64_t nTotalLogicalPages;
  uint64_t maxPagesBeforeGC;
  uint64_t tick;
  uint64_t valid;
  uint64_t invalid;
  FILLING_MODE mode;

  Request req(param.ioUnitInPage);

  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization started");

  nTotalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
  // mjo: By modifying this field, you can accelerate the first GC
  nPagesToWarmup =
      nTotalLogicalPages * conf.readDouble(CONFIG_FTL, FTL_FILL_RATIO);
  nPagesToInvalidate =
      nTotalLogicalPages * conf.readDouble(CONFIG_FTL, FTL_INVALID_PAGE_RATIO);
  mode = (FILLING_MODE)conf.readUint(CONFIG_FTL, FTL_FILLING_MODE);
  maxPagesBeforeGC =
      param.pagesInBlock *
      (param.totalPhysicalBlocks *
           (1 - conf.readDouble(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO)) -
       param.pageCountToMaxPerf);  // # free blocks to maintain

  if (nPagesToWarmup + nPagesToInvalidate > maxPagesBeforeGC) {
    warn("ftl: Too high filling ratio. Adjusting invalidPageRatio.");
    nPagesToInvalidate = maxPagesBeforeGC - nPagesToWarmup;
  }

  debugprint(LOG_FTL_PAGE_MAPPING, "Total logical pages: %" PRIu64,
             nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total logical pages to fill: %" PRIu64 " (%.2f %%)",
             nPagesToWarmup, nPagesToWarmup * 100.f / nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total invalidated pages to create: %" PRIu64 " (%.2f %%)",
             nPagesToInvalidate,
             nPagesToInvalidate * 100.f / nTotalLogicalPages);

  req.ioFlag.set();

  // Step 1. Filling
  if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Step 2. Invalidating
  if (mode == FILLING_MODE_0) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else if (mode == FILLING_MODE_1) {
    // Random
    // We can successfully restrict range of LPN to create exact number of
    // invalid pages because we wrote in sequential mannor in step 1.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nPagesToWarmup - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Report
  calculateTotalPages(valid, invalid);
  debugprint(LOG_FTL_PAGE_MAPPING, "Filling finished. Page status:");
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total valid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             valid, valid * 100.f / nTotalLogicalPages, nPagesToWarmup,
             (int64_t)(valid - nPagesToWarmup));
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total invalid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             invalid, invalid * 100.f / nTotalLogicalPages, nPagesToInvalidate,
             (int64_t)(invalid - nPagesToInvalidate));
  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization finished");

  return true;
}

void PageMapping::read(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.ioFlag.count() > 0) {
    readInternal(req, tick);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "READ  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.lpn, begin, tick, tick - begin);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ);
}

void PageMapping::write(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.ioFlag.count() > 0) {
    writeInternal(req, tick);

    auto isHot =
        salvationConfig.hotAddressTable.contains(req.lpn) ? "hot" : "cold";
    debugprint(LOG_FTL_PAGE_MAPPING,
               "WRITE | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               "), %s",
               req.lpn, begin, tick, tick - begin, isHot);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE);
}

void PageMapping::trim(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  trimInternal(req, tick);

  debugprint(LOG_FTL_PAGE_MAPPING,
             "TRIM  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
             ")",
             req.lpn, begin, tick, tick - begin);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM);
}

void PageMapping::format(LPNRange &range, uint64_t &tick) {
  PAL::Request req(param.ioUnitInPage);
  std::vector<uint32_t> list;

  req.ioFlag.set();

  for (auto iter = table.begin(); iter != table.end();) {
    if (iter->first >= range.slpn && iter->first < range.slpn + range.nlp) {
      auto &mappingList = iter->second;

      // Do trim
      for (uint32_t idx = 0; idx < bitsetSize; idx++) {
        auto &mapping = mappingList.at(idx);
        for (auto &m : blkClusters) {
          auto &blocks = m.blocks;
          auto block = blocks.find(mapping.first);

          if (block == blocks.end()) {
            if (&m + 1 == std::end(blkClusters)) {
              panic("Block is not in use");
            }
            else {
              continue;
            }
          }

          block->second.invalidate(mapping.second, idx);
        }

        // Collect block indices
        list.push_back(mapping.first);
      }

      iter = table.erase(iter);
    }
    else {
      iter++;
    }
  }

  // Get blocks to erase
  std::sort(list.begin(), list.end());
  auto last = std::unique(list.begin(), list.end());
  list.erase(last, list.end());

  // Do GC only in specified blocks
  doGarbageCollection(list, tick);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::FORMAT);
}

Status *PageMapping::getStatus(uint64_t lpnBegin, uint64_t lpnEnd) {
  status.freePhysicalBlocks = cold.freeBlocks.size() + hot.freeBlocks.size();

  if (lpnBegin == 0 && lpnEnd >= status.totalLogicalPages) {
    status.mappedLogicalPages = table.size();
  }
  else {
    status.mappedLogicalPages = 0;

    for (uint64_t lpn = lpnBegin; lpn < lpnEnd; lpn++) {
      if (table.count(lpn) > 0) {
        status.mappedLogicalPages++;
      }
    }
  }

  return &status;
}

float PageMapping::freeBlockRatio() {
  return (float)(cold.freeBlocks.size() + hot.freeBlocks.size()) /
         param.totalPhysicalBlocks;
}

uint32_t PageMapping::convertBlockIdx(uint32_t blockIdx) {
  return blockIdx % param.pageCountToMaxPerf;
}

std::unordered_map<uint32_t, Block>::iterator PageMapping::getFrontier(
    Bitset &iomap, BlockCluster &c) {
  return c.blocks.find(_getLastFreeBlockIdx(iomap, c));
}

uint32_t PageMapping::_getFreeBlock(uint32_t idx, BlockCluster &c) {
  uint32_t blockIndex = 0;

  if (idx >= param.pageCountToMaxPerf) {
    panic("Index out of range");
  }

  // mjo: If hot blocks are short on free blocks, then borrow free blocks from
  // cold ones. IDK why but there's trouble that empty != (size() == 0).
  if (c.freeBlocks.size() == 0) {
    auto spareIdx = (&c - blkClusters.data() + 1) % blkClusters.size();
    auto &spare = blkClusters[spareIdx];

    auto sz = 1;
    borrowFreeBlocks(spare, c, sz);

    debugprint(LOG_FTL_PAGE_MAPPING, "Borrow %lu free block from %s blocks.",
               sz, spareIdx == HOT ? "hot" : "cold");
  }

  if (c.freeBlocks.size() > 0) {
    // Search block which is blockIdx % param.pageCountToMaxPerf == idx
    auto iter = find_if(
        c.freeBlocks.begin(), c.freeBlocks.end(), [this, idx](Block &b) {
          return b.getBlockIndex() % this->param.pageCountToMaxPerf == idx;
        });

    // Sanity check
    if (iter == c.freeBlocks.end()) {
      // Just use first one
      iter = c.freeBlocks.begin();
      blockIndex = iter->getBlockIndex();
    }
    else {
      blockIndex = iter->getBlockIndex();
    }

    // Insert found block to block list
    if (c.blocks.find(blockIndex) != c.blocks.end()) {
      panic("getFreeBlock: Corrupted");
    }

    c.blocks.emplace(blockIndex, std::move(*iter));

    // Remove found block from free block list
    c.freeBlocks.erase(iter);
  }
  else {
    panic("No free block left");
  }

  return blockIndex;
}

uint32_t PageMapping::_getLastFreeBlockIdx(Bitset &iomap, BlockCluster &c) {
  if (!bRandomTweak || (c.lastFreeBlockIOMap & iomap).any()) {
    // Update lastFreeBlockIndex
    c.lastFreeBlockIndex++;

    if (c.lastFreeBlockIndex == param.pageCountToMaxPerf) {
      c.lastFreeBlockIndex = 0;
    }

    c.lastFreeBlockIOMap = iomap;
  }
  else {
    c.lastFreeBlockIOMap |= iomap;
  }

  auto freeBlock = c.blocks.find(c.lastFreeBlock.at(c.lastFreeBlockIndex));

  // Sanity check
  if (freeBlock == c.blocks.end()) {
    panic("getLastFreeBlock: Corrupted");
  }

  // If current free block is full, get next block.
  // But not an on-demand way, rather preemptive way.
  uint32_t result = c.lastFreeBlock.at(c.lastFreeBlockIndex);

  // mjo: Next: the last page
  // Next + 1: page.end()
  if (freeBlock->second.getNextWritePageIndex() + 1 == param.pagesInBlock) {
    c.lastFreeBlock.at(c.lastFreeBlockIndex) =
        _getFreeBlock(c.lastFreeBlockIndex, c);

    bReclaimMore = true;
  }
  return result;
}

// calculate weight of each block regarding victim selection policy
void PageMapping::calculateVictimWeight(
    std::vector<std::pair<uint32_t, float>> &weight, const EVICT_POLICY policy,
    uint64_t tick, BlockCluster &c) {
  float temp;

  switch (policy) {
    case POLICY_GREEDY:
    case POLICY_RANDOM:
    case POLICY_DCHOICE:
      for (auto &iter : c.blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          // mjo: Pass not fully written blocks
          continue;
        }

        // mjo: Store fully written blocks
        weight.push_back({iter.first, iter.second.getValidPageCountRaw()});
      }

      break;
    case POLICY_COST_BENEFIT:
      for (auto &iter : c.blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          continue;
        }

        temp = (float)(iter.second.getValidPageCountRaw()) / param.pagesInBlock;

        weight.push_back(
            {iter.first,
             temp / ((1 - temp) * (tick - iter.second.getLastAccessedTime()))});
      }

      break;
    default:
      panic("Invalid evict policy");
  }
}

/**
 * retList holds both of hot and cold blocks.
 */
void PageMapping::selectVictimBlock(std::vector<uint32_t> &retList,
                                    uint64_t &tick) {
  static const GC_MODE mode = (GC_MODE)conf.readInt(CONFIG_FTL, FTL_GC_MODE);
  static const EVICT_POLICY policy =
      (EVICT_POLICY)conf.readInt(CONFIG_FTL, FTL_GC_EVICT_POLICY);
  static uint32_t dChoiceParam =
      conf.readUint(CONFIG_FTL, FTL_GC_D_CHOICE_PARAM);
  // mjo: nBlocks variable is loaded from SSD configuration file.
  // In many cases, GCReclaimBlocks value is set to 1, so the code evicts
  // only one block
  uint64_t nBlocks = conf.readUint(CONFIG_FTL, FTL_GC_RECLAIM_BLOCK);

  retList.clear();

  // Calculate number of blocks to reclaim
  if (mode == GC_MODE_0) {
    // DO NOTHING
  }
  else if (mode == GC_MODE_1) {
    static const float t =
        conf.readDouble(CONFIG_FTL, FTL_GC_RECLAIM_THRESHOLD);

    nBlocks = param.totalPhysicalBlocks * t -
              (cold.freeBlocks.size() + hot.freeBlocks.size());
  }
  else {
    panic("Invalid GC mode");
  }

  // reclaim one more if last free block fully used
  if (bReclaimMore) {
    nBlocks += param.pageCountToMaxPerf;

    bReclaimMore = false;
  }

  // mjo: Select blocks from for each hot and cold.
  for (auto &m : blkClusters) {
    // Calculate weights of all blocks
    // mjo: Get fully written blocks with their valid page ratio
    std::vector<std::pair<uint32_t, float>> weight;
    calculateVictimWeight(weight, policy, tick, m);

    if (policy == POLICY_RANDOM || policy == POLICY_DCHOICE) {
      uint64_t randomRange =
          policy == POLICY_RANDOM ? nBlocks : dChoiceParam * nBlocks;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<uint64_t> dist(0, weight.size() - 1);
      std::vector<std::pair<uint32_t, float>> selected;

      while (selected.size() < randomRange) {
        uint64_t idx = dist(gen);

        if (weight.at(idx).first < std::numeric_limits<uint32_t>::max()) {
          selected.push_back(weight.at(idx));
          weight.at(idx).first = std::numeric_limits<uint32_t>::max();
        }
      }

      weight = std::move(selected);
    }

    // Sort weights
    std::sort(
        weight.begin(), weight.end(),
        [](std::pair<uint32_t, float> a, std::pair<uint32_t, float> b) -> bool {
          return a.second < b.second;
        });

    // Select victims from the blocks with the lowest weight
    nBlocks = MIN(nBlocks, weight.size());

    // mjo: Store logical block numbers
    for (uint64_t i = 0; i < nBlocks; i++) {
      retList.push_back(weight.at(i).first);
    }
    const char *msg = &m == &cold ? "COLD" : "HOT";
    debugprint(LOG_FTL_PAGE_MAPPING,
               "PreGC | %-9s | %u blocks will be reclaimed", msg, nBlocks);
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::SELECT_VICTIM_BLOCK);
}

void PageMapping::doGarbageCollection(std::vector<uint32_t> &blocksToReclaim,
                                      uint64_t &tick) {
  PAL::Request req(param.ioUnitInPage);
  std::vector<PAL::Request> readRequests;
  std::vector<PAL::Request> writeRequests;
  std::vector<PAL::Request> eraseRequests;
  std::vector<uint64_t> lpns;
  Bitset bit(param.ioUnitInPage);
  uint64_t beginAt = 0;
  uint64_t readFinishedAt = tick;
  uint64_t writeFinishedAt = tick;
  uint64_t eraseFinishedAt = tick;

  if (blocksToReclaim.size() == 0) {
    return;
  }

  // For all blocks to reclaim, collecting request structure only
  // mjo: blocksToReclaim is sorted by valid-page-ratio
  for (auto &iter : blocksToReclaim) {
    auto cIt = std::find_if(blkClusters.begin(), blkClusters.end(),
                            [iter](BlockCluster &c) {
                              return c.blocks.find(iter) != c.blocks.end();
                            });

    if (cIt == std::end(blkClusters)) {
      panic("Invalid block");
    }

    auto block = cIt->blocks.find(iter);
    validPageAcc += block->second.getValidPageCountRaw();
    validPageCnt++;

    // Copy valid pages to free block
    for (uint32_t pageIndex = 0; pageIndex < param.pagesInBlock; pageIndex++) {
      // Valid?
      if (block->second.getPageInfo(pageIndex, lpns, bit)) {
        if (!bRandomTweak) {
          bit.set();
        }

        // mjo: Thanks to the implementation, I don't have to struggle with
        // modifying fetching free blocks since for every page to reclaim it
        // fetches a free block.
        // Retrive free block
        auto freeBlockIter = getFrontier(bit, cold);

        // Issue Read
        req.blockIndex = block->first;
        req.pageIndex = pageIndex;
        req.ioFlag = bit;

        readRequests.push_back(req);

        // Update mapping table
        uint32_t newBlockIdx = freeBlockIter->first;

        // Normally, bitsetSize is just 1 since superpage is disabled.
        for (uint32_t idx = 0; idx < bitsetSize; idx++) {
          // mjo: Invalidate page only if the page is valid.
          // In other words, trimmed pages are skipped and you can save the
          // time!
          //
          // If TRIM is not supported, then the following problem arises:
          //	Even though a file is deleted in an OS's view, SSDs don't know
          // about the deletion. 	So SSDs have to copy the pages,
          // corresponding to the deleted file, on every GC 	until new write
          // request with the same LBA is queued to the SSDs.
          if (bit.test(idx)) {
            // Invalidate
            block->second.invalidate(pageIndex, idx);

            auto mappingList = table.find(lpns.at(idx));

            if (mappingList == table.end()) {
              panic("Invalid mapping table entry");
            }

            pDRAM->read(&(*mappingList), 8 * param.ioUnitInPage, tick);

            auto &mapping = mappingList->second.at(idx);

            uint32_t newPageIdx =
                freeBlockIter->second.getNextWritePageIndex(idx);

            mapping.first = newBlockIdx;
            mapping.second = newPageIdx;

            // mjo: Copy data
            freeBlockIter->second.write(newPageIdx, lpns.at(idx), idx, beginAt);

            // Issue Write
            req.blockIndex = newBlockIdx;
            req.pageIndex = newPageIdx;

            if (bRandomTweak) {
              req.ioFlag.reset();
              req.ioFlag.set(idx);
            }
            else {
              req.ioFlag.set();
            }

            writeRequests.push_back(req);

            stat.validPageCopies++;
          }
        }

        stat.validSuperPageCopies++;
      }
    }

    // Erase block
    req.blockIndex = block->first;
    req.pageIndex = 0;
    req.ioFlag.set();

    eraseRequests.push_back(req);
  }

  // Do actual I/O here
  // This handles PAL2 limitation (SIGSEGV, infinite loop, or so-on)
  for (auto &iter : readRequests) {
    beginAt = tick;

    pPAL->read(iter, beginAt);

    readFinishedAt = MAX(readFinishedAt, beginAt);
  }

  for (auto &iter : writeRequests) {
    beginAt = readFinishedAt;

    pPAL->write(iter, beginAt);

    writeFinishedAt = MAX(writeFinishedAt, beginAt);
  }

  for (auto &iter : eraseRequests) {
    beginAt = readFinishedAt;

    eraseInternal(iter, beginAt);

    eraseFinishedAt = MAX(eraseFinishedAt, beginAt);
  }

  tick = MAX(writeFinishedAt, eraseFinishedAt);
  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::DO_GARBAGE_COLLECTION);
}

void PageMapping::readInternal(Request &req, uint64_t &tick) {
  PAL::Request palRequest(req);
  uint64_t beginAt;
  uint64_t finishedAt = tick;

  auto mappingList = table.find(req.lpn);

  if (mappingList != table.end()) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
    }

    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        // mjo: block#, page#
        auto &mapping = mappingList->second.at(idx);

        if (mapping.first < param.totalPhysicalBlocks &&
            mapping.second < param.pagesInBlock) {
          palRequest.blockIndex = mapping.first;
          palRequest.pageIndex = mapping.second;

          // mjo: Random I/O tweak is used when superpage mode is enabled.
          // The authors said it helps random write performance, I'm not sure
          // what it is though.
          if (bRandomTweak) {
            palRequest.ioFlag.reset();
            palRequest.ioFlag.set(idx);
          }
          else {
            palRequest.ioFlag.set();
          }

          auto &blocks =
              std::find_if(blkClusters.begin(), blkClusters.end(),
                           [idx = palRequest.blockIndex](BlockCluster &c) {
                             return c.blocks.find(idx) != c.blocks.end();
                           })
                  ->blocks;
          auto block = blocks.find(palRequest.blockIndex);

          if (block == blocks.end()) {
            panic("Block is not in use");
          }

          beginAt = tick;

          block->second.read(palRequest.pageIndex, idx, beginAt);
          pPAL->read(palRequest, beginAt);

          finishedAt = MAX(finishedAt, beginAt);
        }
      }
    }

    tick = finishedAt;
    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ_INTERNAL);
  }
}

void PageMapping::writeInternal(Request &req, uint64_t &tick, bool sendToPAL) {
  PAL::Request palRequest(
      req);  // mjo: Copy ioFlag. IOFlag means pages in a superpage
  std::unordered_map<uint32_t, Block>::iterator blockIter;
  // mjo: table holds LPN -> PPN mappings
  auto mappingList =
      table.find(req.lpn);  // mjo: a vector of <block#, page# in a block>
  uint64_t beginAt;
  uint64_t finishedAt = tick;
  bool readBeforeWrite = false;

  // mjo: Step 0: Update hotAddressTable.
  if (HotAddressTable::enabled) {
    salvationConfig.hotAddressTable.update(req.lpn);
  }

  // mjo: Step 1: Invalidate previously written  page(s).

  if (mappingList != table.end()) {
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      // Do IO operation per page, not superpage!
      if (req.ioFlag.test(idx) || !bRandomTweak) {
        auto &mapping =
            mappingList->second.at(idx);  // mjo: <block#, page# in block>

        if (mapping.first < param.totalPhysicalBlocks &&
            mapping.second < param.pagesInBlock) {
          blockIter = std::find_if(blkClusters.begin(), blkClusters.end(),
                                   [mapping](BlockCluster &c) {
                                     return c.blocks.find(mapping.first) !=
                                            c.blocks.end();
                                   })
                          ->blocks.find(mapping.first);

          // Invalidate current page
          blockIter->second.invalidate(mapping.second, idx);

          // mjo: Since SSDs cannnot update data,
          // we need to invalidate the previous data before overwrite them.
        }
      }
    }
  }
  else {
    // Create empty mapping
    auto ret = table.emplace(
        req.lpn,
        std::vector<std::pair<uint32_t, uint32_t>>(
            bitsetSize, {param.totalPhysicalBlocks, param.pagesInBlock}));

    if (!ret.second) {
      panic("Failed to insert new mapping");
    }

    mappingList = ret.first;
  }

  // mjo: Step 2: Write data to new page(s)

  // Write data to free block
  // mjo: Get a free block from the free block list.
  // mjo: Unordered_map.find returns an iterator which contains <key, value>
  bool isHot = HotAddressTable::enabled &&
               salvationConfig.hotAddressTable.contains(req.lpn);
  if (isHot) {
    blockIter = getFrontier(req.ioFlag, hot);
  }
  else {
    blockIter = getFrontier(req.ioFlag, cold);
  }  // mjo: <ppn of the block, Block instance>

  if (blockIter == cold.blocks.end()) {
    panic("No such block");
  }

  Block &block = blockIter->second;

  if (sendToPAL) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
      pDRAM->write(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
      pDRAM->write(&(*mappingList), 8, tick);
    }
  }

  if (!bRandomTweak && !req.ioFlag.all()) {
    // We have to read old data
    readBeforeWrite = true;
  }

  for (uint32_t idx = 0; idx < bitsetSize; idx++) {
    // Do IO operation per page, not superpage!
    if (req.ioFlag.test(idx) || !bRandomTweak) {
      // mjo: Use empty page in the same block instead of get a page from
      // another block.
      uint32_t pageIndex = block.getNextWritePageIndex(idx);
      auto &mapping = mappingList->second.at(idx);

      beginAt = tick;

      // mjo: idx corresponds to an index of a superpage, so we can just ignore
      // it :) In other words, only pageIndex matters
      block.write(pageIndex, req.lpn, idx, beginAt);

      // Read old data if needed (Only executed when bRandomTweak = false)
      // Maybe some other init procedures want to perform 'partial-write'
      // So check sendToPAL variable
      if (readBeforeWrite && sendToPAL) {
        palRequest.blockIndex = mapping.first;
        palRequest.pageIndex = mapping.second;

        // We don't need to read old data
        palRequest.ioFlag = req.ioFlag;
        palRequest.ioFlag.flip();

        pPAL->read(palRequest, beginAt);
      }

      // update mapping to table
      mapping.first = blockIter->first;
      mapping.second = pageIndex;

      if (sendToPAL) {
        palRequest.blockIndex = blockIter->first;
        palRequest.pageIndex = pageIndex;

        if (bRandomTweak) {
          palRequest.ioFlag.reset();
          palRequest.ioFlag.set(idx);
        }
        else {
          palRequest.ioFlag.set();
        }

        pPAL->write(palRequest, beginAt);
      }

      finishedAt = MAX(finishedAt, beginAt);
    }
  }

  // Exclude CPU operation when initializing
  if (sendToPAL) {
    tick = finishedAt;
    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE_INTERNAL);
  }

  // GC if needed
  // I assumed that init procedure never invokes GC
  static float gcThreshold =
      conf.readDouble(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO);

  if (freeBlockRatio() < gcThreshold) {
    if (!sendToPAL) {
      panic("ftl: GC triggered while in initialization");
    }

    std::vector<uint32_t> list;
    uint64_t beginAt = tick;

    selectVictimBlock(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | On-demand | %u blocks will be reclaimed", list.size());

    doGarbageCollection(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | Done | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")", tick,
               beginAt, beginAt - tick);

    stat.gcCount++;
    stat.reclaimedBlocks += list.size();
  }
}

void PageMapping::trimInternal(Request &req, uint64_t &tick) {
  auto mappingList = table.find(req.lpn);

  if (mappingList != table.end()) {
    if (bRandomTweak) {
      pDRAM->read(&(*mappingList), 8 * req.ioFlag.count(), tick);
    }
    else {
      pDRAM->read(&(*mappingList), 8, tick);
    }

    // Do trim
    for (uint32_t idx = 0; idx < bitsetSize; idx++) {
      auto &mapping = mappingList->second.at(idx);
      auto &blocks =
          find_if(blkClusters.begin(), blkClusters.end(),
                  [mapping](BlockCluster &c) {
                    return c.blocks.find(mapping.first) != c.blocks.end();
                  })
              ->blocks;
      auto block = blocks.find(mapping.first);

      if (block == blocks.end()) {
        panic("Block is not in use");
      }

      block->second.invalidate(mapping.second, idx);
    }

    // Remove mapping
    table.erase(mappingList);

    tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM_INTERNAL);
  }
}

void PageMapping::eraseInternal(PAL::Request &req, uint64_t &tick) {
  // static uint64_t threshold =
  //    conf.readUint(CONFIG_FTL, FTL_BAD_BLOCK_THRESHOLD);
  auto m = std::find_if(blkClusters.begin(), blkClusters.end(), [req](auto &m) {
    return m.blocks.find(req.blockIndex) != m.blocks.end();
  });
  auto &blocks = m->blocks;
  auto &freeBlocks = m->freeBlocks;
  auto block = blocks.find(req.blockIndex);

  // Sanity checks
  if (block == blocks.end()) {
    panic("No such block");
  }

  if (block->second.getValidPageCount() != 0) {
    panic("There are valid pages in victim block");
  }

  // Erase block
  block->second.erase();

  pPAL->erase(req, tick);

  // Check erase count
  uint32_t erasedCount = block->second.getEraseCount();

  auto blockIsAlive = [&block, this] {
    float unavailablePageRatio = block->second.getUnavailablePageRatio();
    if (salvationConfig.enabled) {
      return unavailablePageRatio <
             this->salvationConfig.unavailablePageThreshold;
    }
    else {
      return unavailablePageRatio == 0;
    }
  };
  if (blockIsAlive()) {
    // Reverse search
    auto iter = freeBlocks.end();

    // mjo: TODO: Salvaged bad block의 수명은..?
    while (true) {
      iter--;

      if (iter->getEraseCount() <= erasedCount) {
        // emplace: insert before pos
        iter++;

        break;
      }

      if (iter == freeBlocks.begin()) {
        break;
      }
    }

    // Insert block to free block list
    if (HotAddressTable::enabled && block->second.getUnavailablePageCount()) {
      hot.freeBlocks.emplace(iter, std::move(block->second));
    }
    else {
      cold.freeBlocks.emplace(iter, std::move(block->second));
    }
  }
  else {
    // Otherwise, treated as bad-block
  }

  // Remove block from block list
  blocks.erase(block);

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::ERASE_INTERNAL);
}

void PageMapping::borrowFreeBlocks(BlockCluster &from, BlockCluster &to,
                                   uint32_t n) {
  auto b = from.freeBlocks.begin();
  auto e = next(b, n);
  to.freeBlocks.splice(to.freeBlocks.end(), from.freeBlocks, b, e);
}

float PageMapping::calculateWearLeveling() {
  uint64_t totalEraseCnt = 0;
  uint64_t sumOfSquaredEraseCnt = 0;
  uint64_t numOfBlocks = param.totalLogicalBlocks;
  uint64_t eraseCnt;

  for (auto &m : blkClusters) {
    auto &blocks = m.blocks;
    for (auto &iter : blocks) {
      eraseCnt = iter.second.getEraseCount();
      totalEraseCnt += eraseCnt;
      sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
    }
  }

  // freeBlocks is sorted
  // Calculate from backward, stop when eraseCnt is zero
  for (auto &m : blkClusters) {
    auto &freeBlocks = m.freeBlocks;
    for (auto riter = freeBlocks.rbegin(); riter != freeBlocks.rend();
         riter++) {
      eraseCnt = riter->getEraseCount();

      if (eraseCnt == 0) {
        break;
      }

      totalEraseCnt += eraseCnt;
      sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
    }
  }

  if (sumOfSquaredEraseCnt == 0) {
    return -1;  // no meaning of wear-leveling
  }

  return (float)totalEraseCnt * totalEraseCnt /
         (numOfBlocks * sumOfSquaredEraseCnt);
}

void PageMapping::calculateTotalPages(uint64_t &valid, uint64_t &invalid) {
  valid = 0;
  invalid = 0;

  for (auto &m : blkClusters) {
    for (auto &iter : m.blocks) {
      valid += iter.second.getValidPageCount();
      invalid += iter.second.getDirtyPageCount();
    }
  }
}

void PageMapping::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "page_mapping.gc.count";
  temp.desc = "Total GC count";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.reclaimed_blocks";
  temp.desc = "Total reclaimed blocks in GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.superpage_copies";
  temp.desc = "Total copied valid superpages during GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.page_copies";
  temp.desc = "Total copied valid pages during GC";
  list.push_back(temp);

  // For the exact definition, see following paper:
  // Li, Yongkun, Patrick PC Lee, and John Lui.
  // "Stochastic modeling of large-scale solid-state storage systems: analysis,
  // design tradeoffs and optimization." ACM SIGMETRICS (2013)
  temp.name = prefix + "page_mapping.wear_leveling";
  temp.desc = "Wear-leveling factor";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.valid_pages";
  temp.desc = "The average number of valid pages in GCed blocks";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.hot";
  temp.desc = "The number of hot addresses";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.hot_capacity";
  temp.desc = "Capacity of hot addresses";
  list.push_back(temp);
}

void PageMapping::getStatValues(std::vector<double> &values) {
  values.push_back(stat.gcCount);
  values.push_back(stat.reclaimedBlocks);
  values.push_back(stat.validSuperPageCopies);
  values.push_back(stat.validPageCopies);
  values.push_back(calculateWearLeveling());
  values.push_back(!validPageCnt ? 0 : (double)validPageAcc / validPageCnt);
  values.push_back(salvationConfig.hotAddressTable.hotSize());
  values.push_back(salvationConfig.hotAddressTable.size());
}

void PageMapping::resetStatValues() {
  memset(&stat, 0, sizeof(stat));
}

}  // namespace FTL

}  // namespace SimpleSSD
