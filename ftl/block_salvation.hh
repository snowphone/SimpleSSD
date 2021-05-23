#pragma once

#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "ftl/bad_page_table.hh"
#include "ftl/hot_address_table.hh"
#include "ftl/error_model.hh"
#include "ftl/prev_work/smt.hh"

namespace SimpleSSD {

namespace FTL {

class Salvation {
  std::unique_ptr<ErrorModel> pModel = nullptr;

 public:
  bool enabled;

  BadPageTable badPageTable;
  std::unique_ptr<SMT> smt;
  HotAddressTable hotAddressTable;
  double unavailablePageThreshold;

  void setModel(std::unique_ptr<ErrorModel> &&m);

  double getPer();

  double getBer();

  std::string to_string();
};

double probability();

bool probability(long double p);

uint64_t pick(uint64_t min, uint64_t max);

std::vector<uint64_t> sample(uint64_t min, uint64_t max, uint64_t numSamples);

}  // namespace FTL
}  // namespace SimpleSSD
