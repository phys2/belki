#include "features.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp> // for calchist
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

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

Features::Range log_valid(const Features::Range &range)
{
	double lb;
	if (range.max > 10000)
		lb = 1;
	else if (range.max > 100)
		lb = 0.01;
	else if (range.max > 10)
		lb = 0.001;
	else
		lb = 0.0001;
	return {std::max(range.min, lb), std::max(range.max, lb)};
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

vec with_cutoff(const vec &feats, const vec &scores, double threshold)
{
	vec ret(feats.size());
	tbb::parallel_for(size_t(0), feats.size(), [&] (size_t p) {
		auto &source = feats[p];
		auto &target = ret[p];
		auto &score = scores[p];
		target.resize(source.size());
		for (size_t i = 0; i < source.size(); ++i)
			target[i] = (score[i] <= threshold) * source[i];
	});
	return ret;
}

void apply_cutoff(vec &feats, vec &scores, double threshold)
{
	tbb::parallel_for(size_t(0), feats.size(), [&] (size_t p) {
		auto &feat = feats[p];
		auto &score = scores[p];
		for (size_t i = 0; i < feat.size(); ++i) {
			if (score[i] > threshold) {
				feat[i] = 0.;
				score[i] = threshold; // normalize to new limit
			}
		}
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

template<>
double distance<Distance::EUCLIDEAN>(const std::vector<double> &a, const std::vector<double> &b)
{
	return cv::norm(a, b, cv::NORM_L2);
}

template<>
double distance<Distance::CROSSCORREL>(const std::vector<double> &a, const std::vector<double> &b)
{
	double corr1 = 0., corr2 = 0., crosscorr = 0.;
	for (unsigned i = 0; i < a.size(); ++i) {
		auto v1 = a[i], v2 = b[i];
		corr1 += v1*v1;
		corr2 += v2*v2;
		crosscorr += v1*v2;
	}
	if (corr1 == 0. && corr2 == 0.)
		return 0.;
	return crosscorr / std::sqrt(corr1*corr2);
}

template<>
double distance<Distance::COSINE>(const std::vector<double> &a, const std::vector<double> &b)
{
	return std::acos(distance<Distance::CROSSCORREL>(a, b));
}

template<>
double distance<Distance::PEARSON>(const std::vector<double> &a, const std::vector<double> &b)
{
	std::vector<double> aa, bb;
	cv::subtract(a, cv::mean(a), aa);
	cv::subtract(b, cv::mean(b), bb);
	return distance<Distance::CROSSCORREL>(aa, bb);
}

template<>
double distance<Distance::EMD>(const std::vector<double> &a, const std::vector<double> &b)
{
	cv::Mat1f ina(a.size(), 1 + 1, 1.f); // weight + value
	cv::Mat1f inb(b.size(), 1 + 1, 1.f); // weight + value
	std::copy(a.cbegin(), a.cend(), ina.col(1).begin());
	std::copy(b.cbegin(), b.cend(), inb.col(1).begin());
	// use L1 here as we have scalar inputs anyway
	return (double)cv::EMD(ina, inb, cv::DIST_L1);
}

std::function<double(const std::vector<double> &a, const std::vector<double> &b)>
distfun(Distance measure)
{
	switch (measure) {
	case Distance::COSINE: return distance<Distance::COSINE>;
	case Distance::CROSSCORREL: return distance<Distance::CROSSCORREL>;
	case Distance::PEARSON: return distance<Distance::PEARSON>;
	case Distance::EMD: return distance<Distance::EMD>;
	default: return distance<Distance::EUCLIDEAN>;
	}
}

Features::Stats computeStats(const vec &feats, bool withRange, const std::vector<size_t> &filter)
{
	if (feats.empty())
		return {};

	Features::Stats ret;

	auto len = feats.front().size();
	for (auto v : {&ret.mean, &ret.stddev,
		           &ret.min, &ret.max,
		           &ret.quant25, &ret.quant50, &ret.quant75})
		v->resize(len);

	/* compute statistics per-dimension */
	bool filtered = !filter.empty();
	auto nFeats = filtered ? filter.size() : feats.size();
	auto statsPerDim = [&] (unsigned dim) {
		std::vector<double> f(nFeats);
		if (filter.empty()) {
			for (size_t j = 0; j < nFeats; ++j)
				f[j] = feats[j][dim];
		} else {
			for (size_t j = 0; j < nFeats; ++j)
				f[j] = feats[filter[j]][dim];
		}

		cv::Scalar m, s;
		cv::meanStdDev(f, m, s);
		ret.mean[dim] = m[0];
		ret.stddev[dim] = s[0];

		std::sort(f.begin(), f.end());
		ret.min[dim] = f.front();
		ret.max[dim] = f.back();
		ret.quant25[dim] = f[nFeats / 4];
		ret.quant50[dim] = f[nFeats / 2];
		ret.quant75[dim] = f[(nFeats * 3) / 4];
	};

	if (nFeats < 1000) {
		for (size_t i = 0; i < len; ++i)
			statsPerDim(i);
	} else {
		tbb::parallel_for(size_t(0), len, [&] (size_t i) { statsPerDim(i); });
	}

	// compute overall range afterwards, not to disturb parallel computation above
	if (withRange) {
		ret.range = {ret.min[0], ret.max[0]};
		for (size_t i = 1; i < len; ++i) {
			ret.range.min = std::min(ret.min[i], ret.range.min);
			ret.range.max = std::max(ret.max[i], ret.range.max);
		}
	}

	return ret;
}

}
