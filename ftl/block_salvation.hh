#pragma once

#include <bits/stdint-uintn.h>
#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

namespace SimpleSSD {

namespace FTL {

struct Salvation {
  bool enabled;
  float unavailablePageThreshold;
  float initialBadBlockRatio;
  float initialBadPageRatio;
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

inline std::vector<uint64_t> sample(uint64_t min, uint64_t max, uint64_t numSamples) {
	std::vector<uint64_t> samples(max - min + 1);
	std::iota(samples.begin(),samples.end(), min);
	std::shuffle(samples.begin(), samples.end(), gen);
	samples.resize(numSamples);
	
	return samples;
}


}  // namespace FTL
}  // namespace SimpleSSD
