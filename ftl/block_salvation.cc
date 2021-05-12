#include "ftl/block_salvation.hh"

#include <math.h>

#include <algorithm>
#include <ios>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace SimpleSSD {

namespace FTL {

static std::random_device rd;
static std::mt19937 gen(rd());

void Salvation::setModel(std::unique_ptr<ErrorModel> &&m) {
  this->pModel.swap(m);
}

double Salvation::getPer() {
  return pModel->getPer();
}

double Salvation::getBer() {
  return pModel->getBer();
}

std::string Salvation::to_string() {
  std::stringstream ss;
  ss << "Salvation: " << std::boolalpha << this->enabled
     << " Model: " << this->pModel->to_string()
     << " Hot-cold separation: " << std::boolalpha
     << this->hotAddressTable.enabled;

  return ss.str();
}

double probability() {
  static std::uniform_real_distribution<double> dist(0, 1);

  return dist(gen);
}

bool probability(long double p) {
  return probability() < p;
}

uint64_t pick(uint64_t min, uint64_t max) {
  std::uniform_int_distribution<uint64_t> dist(min, max);

  return dist(gen);
}

std::vector<uint64_t> sample(uint64_t min, uint64_t max, uint64_t numSamples) {
  std::vector<uint64_t> samples(max - min + 1);
  std::iota(samples.begin(), samples.end(), min);
  std::shuffle(samples.begin(), samples.end(), gen);
  samples.resize(numSamples);

  return samples;
}

}  // namespace FTL
}  // namespace SimpleSSD
