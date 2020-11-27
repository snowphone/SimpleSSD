#pragma once

#include <vector>


template<typename T>
std::vector<T> flatten(const std::vector<std::vector<T>>& v) {
	std::vector<T> ret;
	for(auto& it: v) {
		ret.insert(ret.end(), it.begin(), it.end());
	}
	return ret;
}
