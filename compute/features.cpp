#include "features.h"
#include <opencv2/core.hpp>
#include <tbb/tbb.h>

#include <map>

namespace features {

unsigned cutoff_effect(const vec &source, double threshold)
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

void apply_cutoff(vec &feats, const vec &scores, double threshold)
{
	vec ret(feats.size());
	tbb::parallel_for(size_t(0), feats.size(), [&] (size_t p) {
		auto &feat = feats[p];
		auto &score = scores[p];
		for (size_t i = 0; i < feat.size(); ++i)
			feat[i] = (score[i] <= threshold ? feat[i] : 0.);
	});
}

Range::Range(const std::vector<std::vector<double> > &source)
    : min(source.empty() ? 0. : source[0][0]), max(source.empty() ? 0. : source[0][0])
{
	for (auto in : source) {
		double mi, ma;
		cv::minMaxLoc(in, &mi, &ma);
		min = std::min(min, mi);
		max = std::max(max, ma);
	}
}

double Range::scale() const
{
	return 1./(max - min);
}

std::vector<QVector<QPointF>> pointify(const vec &source)
{
	std::vector<QVector<QPointF>> ret;
	for (auto f : source) {
		QVector<QPointF> points(f.size());
		for (size_t i = 0; i < f.size(); ++i)
			points[i] = {(qreal)i, f[i]};
		ret.push_back(std::move(points));
	}
	return ret;
}

QVector<QPointF> scatter(const vec &x, size_t xi, const vec &y, size_t yi)
{
	QVector<QPointF> ret(x.size());
	for (size_t i = 0; i < x.size(); ++i)
		ret[i] = {x[i][xi], y[i][yi]};
	return ret;
}

}
