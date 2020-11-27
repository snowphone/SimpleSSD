#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <vector>

using namespace std;

template<typename T>
double mean(const vector<vector<T>>& v) {
	double ret = 0;
	size_t length = 0;
	for(auto& it: v) {
		length += it.size();
		for(auto& jt: it) {
			ret += jt;
		}
	}
	return ret / length;
}

template<typename T>
double mean(const vector<T>& v) {
	double ret = 0;
	for(auto& it: v) {
		ret += it;
	}
	return ret / v.size();
}

template<typename T>
double standard_deviation(const vector<vector<T>>& v) {
	double m = mean(v);
	double dev = 0;
	size_t length = 0;

	for(auto& it: v) {
		length += it.size();
		for(auto& jt: it) {
			dev += pow((m - jt), 2);
		}
	}
	dev /= length;

	return sqrt(dev);
}

template<typename T>
double standard_deviation(const vector<T>& v) {
	double m = mean(v);
	double dev = 0;

	for(auto& it: v) {
		dev += pow((m - it), 2);
	}
	dev /= v.size();

	return sqrt(dev);
}


/*
 *  Internal implementation of the SMAWK algorithm.
 */
template <typename T>
void _smawk(
        const vector<long>& rows,
        const vector<long>& cols,
        const function<T(long, long)>& lookup,
        vector<long>* result) {
    // Recursion base case
    if (rows.size() == 0) return;

    // ********************************
    // * REDUCE
    // ********************************

    vector<long> _cols;  // Stack of surviving columns
    for (long col : cols) {
        while (true) {
            if (_cols.size() == 0) break;
            long row = rows[_cols.size() - 1];
            if (lookup(row, col) >= lookup(row, _cols.back()))
                break;
            _cols.pop_back();
        }
        if (_cols.size() < rows.size())
            _cols.push_back(col);
    }

    // Call recursively on odd-indexed rows
    vector<long> odd_rows;
    for (unsigned long i = 1; i < rows.size(); i += 2) {
        odd_rows.push_back(rows[i]);
    }
    _smawk(odd_rows, _cols, lookup, result);

    unordered_map<long, long> col_idx_lookup;
    for (unsigned long idx = 0; idx < _cols.size(); ++idx) {
        col_idx_lookup[_cols[idx]] = idx;
    }

    // ********************************
    // * INTERPOLATE
    // ********************************

    // Fill-in even-indexed rows
    long start = 0;
    for (unsigned long r = 0; r < rows.size(); r += 2) {
        long row = rows[r];
        long stop = _cols.size() - 1;
        if (r < rows.size() - 1)
            stop = col_idx_lookup[(*result)[rows[r + 1]]];
        long argmin = _cols[start];
        T min = lookup(row, argmin);
        for (long c = start + 1; c <= stop; ++c) {
            T value = lookup(row, _cols[c]);
            if (c == start || value < min) {
                argmin = _cols[c];
                min = value;
            }
        }
        (*result)[row] = argmin;
        start = stop;
    }
}

/*
 *  Interface for the SMAWK algorithm, for finding the minimum value in each row
 *  of an implicitly-defined totally monotone matrix.
 */
template <typename T>
vector<long> smawk(
        const long num_rows,
        const long num_cols,
        const function<T(long, long)>& lookup) {
    vector<long> result;
    result.resize(num_rows);
    vector<long> rows(num_rows);
    iota(begin(rows), end(rows), 0);
    vector<long> cols(num_cols);
    iota(begin(cols), end(cols), 0);
    _smawk<T>(rows, cols, lookup, &result);
    return result;
}

/*
 *  Calculates cluster costs in O(1) using prefix sum arrays.
 */
template<typename Ty>
class CostCalculator {
    vector<double> cumsum;
    vector<double> cumsum2;

  public:
    CostCalculator(const vector<Ty>& vec, long n) {
        cumsum.push_back(0.0);
        cumsum2.push_back(0.0);
        for (long i = 0; i < n; ++i) {
            double x = vec[i];
            cumsum.push_back(x + cumsum[i]);
            cumsum2.push_back(x * x + cumsum2[i]);
        }
    }

    double calc(long i, long j) {
        if (j < i) return 0.0;
        double mu = (cumsum[j + 1] - cumsum[i]) / (j - i + 1);
        double result = cumsum2[j + 1] - cumsum2[i];
        result += (j - i + 1) * (mu * mu);
        result -= (2 * mu) * (cumsum[j + 1] - cumsum[i]);
        return result;
    }
};

template <typename T>
class Matrix {
    vector<T> data;
    long num_rows;
    long num_cols;

  public:
    Matrix(long num_rows, long num_cols) {
        this->num_rows = num_rows;
        this->num_cols = num_cols;
        data.resize(num_rows * num_cols);
    }

    inline T get(long i, long j) {
        return data[i * num_cols + j];
    }

    inline void set(long i, long j, T value) {
        data[i * num_cols + j] = value;
    }
};

using clusters_t = vector<long>;
using centroids_t = vector<double>;

template<typename Ty>
pair<clusters_t, centroids_t> cluster(
        vector<Ty> array,
        long k) {
    // ***************************************************
    // * Sort input array and save info for de-sorting
    // ***************************************************
	long n = array.size();
	vector<long> clusters(n);
	vector<double> centroids(k);

    vector<long> sort_idxs(n);
    iota(sort_idxs.begin(), sort_idxs.end(), 0);
    sort(
        sort_idxs.begin(),
        sort_idxs.end(),
        [&array](long a, long b) {return array[a] < array[b];});
    vector<long> undo_sort_lookup(n);
    vector<Ty> sorted_array(n);
    for (long i = 0; i < n; ++i) {
        sorted_array[i] = array[sort_idxs[i]];
        undo_sort_lookup[sort_idxs[i]] = i;
    }

    // ***************************************************
    // * Set D and T using dynamic programming algorithm
    // ***************************************************

    // Algorithm as presented in section 2.2 of (Gronlund et al., 2017).

    CostCalculator cost_calculator(sorted_array, n);
    Matrix<double> D(k, n);
    Matrix<long> T(k, n);

    for (long i = 0; i < n; ++i) {
        D.set(0, i, cost_calculator.calc(0, i));
        T.set(0, i, 0);
    }

    for (long k_ = 1; k_ < k; ++k_) {
        auto C = [&D, &k_, &cost_calculator](long i, long j) -> double {
            long col = i < j - 1 ? i : j - 1;
            return D.get(k_ - 1, col) + cost_calculator.calc(j, i);
        };
        vector<long> row_argmins = smawk<double>(n, n, C);
        for (unsigned long i = 0; i < row_argmins.size(); ++i) {
            long argmin = row_argmins[i];
            double min = C(i, argmin);
            D.set(k_, i, min);
            T.set(k_, i, argmin);
        }
    }

    // ***************************************************
    // * Extract cluster assignments by backtracking
    // ***************************************************

    // TODO: This step requires O(kn) memory usage due to saving the entire
    //       T matrix. However, it can be modified so that the memory usage is O(n).
    //       D and T would not need to be retained in full (D already doesn't need
    //       to be fully retained, although it currently is).
    //       Details are in section 3 of (Gr첩nlund et al., 2017).

    vector<double> sorted_clusters(n);

    long t = n;
    long k_ = k - 1;
    long n_ = n - 1;
    // The do/while loop was used in place of:
    //   for (k_ = k - 1; k_ >= 0; --k_)
    // to avoid wraparound of an unsigned type.
    do {
        long t_ = t;
        t = T.get(k_, n_);
        double centroid = 0.0;
        for (long i = t; i < t_; ++i) {
            sorted_clusters[i] = k_;
            centroid += (sorted_array[i] - centroid) / (i - t + 1);
        }
        centroids[k_] = centroid;
        k_ -= 1;
        n_ = t - 1;
    } while (t > 0);

    // ***************************************************
    // * Order cluster assignments to match de-sorted
    // * ordering
    // ***************************************************

    for (long i = 0; i < n; ++i) {
        clusters[i] = sorted_clusters[undo_sort_lookup[i]];
    }

	return {clusters, centroids};
}
