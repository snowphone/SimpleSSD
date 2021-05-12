#include "ftl/error_model.hh"

#include <random>
#include <sstream>

namespace SimpleSSD {

namespace FTL {

static std::random_device rd;
static std::mt19937 gen(rd());

double approx_per(double ber, uint32_t pageSize) {
  return ber * pageSize * BITS_PER_BYTE;
}

static double to_per(double ber, uint32_t pageSize) {
  return 1. - pow((1. - ber), (pageSize * BITS_PER_BYTE));
}

LogNormal::LogNormal(double ber, double sigma, uint32_t pageSize) {
  auto mode_to_mu = [mode = this->mode, sigma] {
    return log(mode) + (sigma * sigma);
  };

  this->setBer(ber);
  this->sigma = sigma;
  this->mode = to_per(ber, pageSize);  // Regarded as a mode of log-normal

  this->mu = mode_to_mu();
  this->dist = std::lognormal_distribution<>(this->mu, this->sigma);
}

double ErrorModel::getBer() {
  return this->ber;
}

void ErrorModel::setBer(double ber) {
  this->ber = ber;
}

double LogNormal::getPer() {
  return dist(gen);
}

std::string LogNormal::to_string() {
  std::stringstream ss;
  ss << "LogNormal. BER: " << this->getBer() << " Mode: " << this->mode
     << " Mean: " << this->mu << " Sigma: " << this->sigma;

  return ss.str();
}

}  // namespace FTL
}  // namespace SimpleSSD
