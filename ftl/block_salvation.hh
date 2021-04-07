#pragma once

#include <vector>

#include "ftl/bad_page_table.hh"
#include "ftl/hot_list.hh"

namespace SimpleSSD {

namespace FTL {

struct Salvation {
  bool enabled;

  BadPageTable badPageTable;
  HotList hotAddressTable;

  double unavailablePageThreshold;
  double ber;
  double per;
};

double probability();

bool probability(long double p);

uint64_t pick(uint64_t min, uint64_t max);

std::vector<uint64_t> sample(uint64_t min, uint64_t max, uint64_t numSamples);

}  // namespace FTL
}  // namespace SimpleSSD
