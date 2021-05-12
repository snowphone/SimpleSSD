
#pragma once

#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "ftl/bad_page_table.hh"
#include "ftl/hot_address_table.hh"

namespace SimpleSSD {

namespace FTL {

enum { BITS_PER_BYTE = 8 };

class ErrorModel {
  double ber = 0;

 public:
  void setBer(double ber);
  double getBer();

  virtual double getPer() = 0;
  virtual std::string to_string() = 0;
};

class LogNormal : public ErrorModel {
  double mu;
  double mode;
  double sigma;
  std::lognormal_distribution<double> dist;

 public:
  LogNormal(double ber, double sigma, uint32_t pageSize);
  double getPer() override;
  std::string to_string() override;
};

}  // namespace FTL
}  // namespace SimpleSSD
