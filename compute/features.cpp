#include "features.h"
#include <opencv2/core.hpp>
#include <tbb/tbb.h>

#include <map>

namespace features {

unsigned cutoff_effect(const featvec &source, double threshold)
{
	auto check = [&] (size_t index) {
		const auto &v = source[index];
		return std::any_of(v.begin(), v.end(), [&] (double value) {
			return value > threshold;
		});
	};
	auto range = tbb::blocked_range<size_t>(size_t(0), source.size());
	return tbb::parallel_reduce(range, unsigned(0), [&] (auto r, unsigned init) {
		for (auto it = r.begin(); it != r.end(); ++it)
			init += check(it);
		return init;
	}, std::plus<unsigned>());
}

void apply_cutoff(featvec &feats, const featvec &scores, double threshold)
{
	featvec ret(feats.size());
	tbb::parallel_for(size_t(0), feats.size(), [&] (size_t p) {
		auto &feat = feats[p];
		auto &score = scores[p];
		for (size_t i = 0; i < feat.size(); ++i)
			feat[i] = (score[i] <= threshold ? feat[i] : 0.);
	});
}

}