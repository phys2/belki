#include "features.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp> // for calchist
#include <tbb/tbb.h>

namespace features {

Features::Range range_of(const vec &source, float fraction)
{
	if (source.empty())
		return {};

	Features::Range ret{source[0][0], source[0][0]};
	for (auto in : source) {
		double mi, ma;
		cv::minMaxLoc(in, &mi, &ma);
		ret.min = std::min(ret.min, mi);
		ret.max = std::max(ret.max, ma);
	}
	if (fraction == 0.f || fraction == 1.0f)
		return ret;

	/* we build a histogram to find "good" data range */
	cv::Mat1f hist; // calcHist always returns 32F
	int bins = 100;
	auto range = { (float)ret.min, (float)(1.0001*ret.max)};

	/* while calcHist() expects InputArrayOfArrays, it does not do what we want
	   when given several arrays at once. So we need a loop w/ accumulate flag */
	for (auto &s : source) {
		/* OpenCV does not support computing histograms on double. Doh! */
		std::vector<cv::Mat> temp(1);
		cv::Mat1d(s).convertTo(temp.front(), cv::DataType<float>::type);

		cv::calcHist(temp, {0}, cv::Mat(), hist, {bins}, range, true); // acc.
	}

	/* we defensively choose bin borders as new range approx. */
	double binsize = (ret.max - ret.min)/(double)bins;
	unsigned needed = (unsigned)std::ceil(
	            (float)(source.size()*source[0].size())*(1.f - fraction));

	auto findFractionBin = [&] (bool reverse = false) {
		// start from first (or last) bin
		auto index = (reverse ? hist.rows - 1 : 0);
		unsigned found = 0;
		index = 0;
		while (found < needed) {
			found += (unsigned)hist[index][0];
			index += (reverse ? -1 : 1); // move on to next bin
		}
		if (reverse) {
			// upper boundary of last outlier bin
			return hist.rows - index - 2;
		} else {
			// lower boundary of last outlier bin
			return index - 1;
		}
	};

	ret.min = ret.min + (binsize * findFractionBin());
	ret.max = ret.max - (binsize * findFractionBin(true));

	return ret;
}

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
