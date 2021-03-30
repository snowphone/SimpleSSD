#pragma once

#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

namespace SimpleSSD {

namespace FTL {

class BadPageTable {
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> table;

 public:
  /**
   * Insert a bad page into the table and merge if there's sequential bad pages
   * in a block. Estimated complexity is O(n) where n is the total number of
   * pages in a block.
   */
  void insert(const uint32_t blkNo, const uint32_t pageNo) {
    auto &bpt = table[blkNo];

    int32_t prevPageNo;
    for (prevPageNo = pageNo - 1; prevPageNo >= 0; --prevPageNo) {
      if (bpt[prevPageNo])
        break;
    }

    // Sequential bad pages: [prevPageNo, prevPageNo + bpt[prevPageNo])
    if (prevPageNo >= 0 && prevPageNo + bpt[prevPageNo] == pageNo) {
      bpt[prevPageNo] += 1;
    }
    else {
      bpt[pageNo] = 1;
    }
  }

  /**
   * Returns the total number of bad pages in a block at O(n).
   */
  uint32_t count(const uint32_t blkNo) {
    uint32_t acc = 0;
    for (auto &[k, v] : table[blkNo]) {
      acc += v;
    }
    return acc;
  }

  /**
   * Returns the number of sequential bad pages at O(1).
   */
  uint32_t get(const uint32_t blkNo, const uint32_t pageNo) {
    auto bbt_it = table.find(blkNo);
    if (bbt_it == table.end()) {
      return 0;
    }
    auto &bpt = bbt_it->second;
    auto it = bpt.find(pageNo);
    return it != bpt.end() ? it->second : 0;
  }
};

struct Salvation {
  bool enabled;

  // Block#, Page #, sequential bad page #
  // std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>
  // badPageTable;
  BadPageTable badPageTable;

  double unavailablePageThreshold;
  double ber;
  double per;
};

static std::random_device rd;
static std::mt19937 gen(rd());

inline double probability() {
  static std::uniform_real_distribution<double> dist(0, 1);

  return dist(gen);
}

inline bool probability(long double p) {
  return probability() < p;
}

inline uint64_t pick(uint64_t min, uint64_t max) {
  std::uniform_int_distribution<uint64_t> dist(min, max);

  return dist(gen);
}

inline std::vector<uint64_t> sample(uint64_t min, uint64_t max,
                                    uint64_t numSamples) {
  std::vector<uint64_t> samples(max - min + 1);
  std::iota(samples.begin(), samples.end(), min);
  std::shuffle(samples.begin(), samples.end(), gen);
  samples.resize(numSamples);

  return samples;
}

}  // namespace FTL
}  // namespace SimpleSSD
