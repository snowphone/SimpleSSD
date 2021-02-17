#pragma once

#include <random>

namespace SimpleSSD {

namespace FTL {

struct Salvation {
  bool enabled;
  float unavailablePageThreshold;
  float initialBadBlockRatio;
};

static std::random_device rd;
static std::mt19937 gen(rd());

inline float probability() {
	static std::uniform_real_distribution<float>  dist(0, 1);

	return dist(gen);
}

inline uint64_t pick(uint64_t min, uint64_t max) {
	std::uniform_int_distribution<uint64_t> dist(min, max);

	return dist(gen);
}


}  // namespace FTL
}  // namespace SimpleSSD
