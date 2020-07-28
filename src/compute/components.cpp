#include "components.h"
#include "dataset.h"
#include "features.h"

#include <tbb/parallel_for.h>
#include <cmath>

namespace components {

std::pair<size_t, size_t> gauss_cover(double mean, double sigma, size_t range, double factor)
{
	auto allowance = factor*sigma;
	auto left = size_t(std::max(0., (mean - allowance))); // works with neg. values
	auto right = size_t(std::min(range - 1., std::ceil(mean + allowance)));
	return {left, right};
}

std::vector<double> generate_gauss(size_t range, double mean, double sigma, double scale)
{
	std::vector<double> ret(range, 0.);
	add_gauss(ret, mean, sigma, scale);
	return ret;
}

void add_gauss(std::vector<double> &target, double mean, double sigma, double scale)
{
	auto twoSigmaSq = 2.*sigma*sigma;
	auto d = scale / std::sqrt(3.14159265358979323846 * twoSigmaSq);

	auto eval = [&] (double x) {
		auto diff = x - mean;
		auto n = std::exp(-(diff*diff) / twoSigmaSq);
		return n*d;
	};

	auto [left, right] = gauss_cover(mean, sigma, target.size());
	for (size_t i = left; i <= right; ++i) {
		target[i] += (eval(i-.5)+eval(i-.25)+eval(i)+eval(i+.25)+eval(i+.5))*0.2;
	}
	/*
	auto inv_sqrt_2pi = 0.3989422804014327 * scale;
	for (size_t i = 0; i < target.size(); ++i) {
		auto a = (i - mean) / sigma;
		target[i] += inv_sqrt_2pi / sigma * std::exp(-0.5 * a * a);
	}*/
}

Matcher::Matcher(std::shared_ptr<const Dataset> data, const std::vector<Components> &comps)
    : data(data), comps(comps)
{
}

void Matcher::matchRange(unsigned reference, std::pair<double,double> range, unsigned topN)
{
	config = {
	    topN,
	    reference,
	    range,
	    {}
	};
	compute();
}

void Matcher::matchComponents(Components reference, unsigned topN, unsigned ignore)
{
	config = {
	    topN,
	    ignore,
	    {},
	    reference
	};
	compute();
}

void Matcher::compute()
{
	auto distance = features::distfun(Distance::COSINE);
	auto b = data->peek<Dataset::Base>();

	/* precompute all distances in parallel */
	std::vector<double> dists(b->features.size(), 0.);

	if (config.refComponents.empty()) {
		std::vector<double> r(b->features[config.reference].begin() + (int)config.range.first,
		        b->features[config.reference].begin() + (int)config.range.second);
		//for (size_t i = 0; i < dists.size(); ++i) {
		tbb::parallel_for(size_t(0), dists.size(), [&] (size_t i) {
			std::vector<double> f(b->features[i].begin() + (int)config.range.first,
			                      b->features[i].begin() + (int)config.range.second);
			dists[i] = distance(f, r);
		});
	} else {
		// TODO: component distances
	}

	auto ranking = rank(dists, config.topN, config.reference);
	emit newRanking(ranking);
}

std::vector<DistIndexPair> Matcher::rank(const std::vector<double> &distances, size_t topN, unsigned ignore)
{
	/* use heap to sort out the top N */
	std::vector<components::DistIndexPair> ret(topN);
	auto first = ret.begin(), last = ret.end();
	// initialize heap with infinity distances
	std::fill(first, last, components::DistIndexPair());

	for (size_t i = 0; i < distances.size(); ++i) {
		if (i == ignore)
			continue;

		if (distances[i] < first->dist) {
			// remove max. value in heap
			std::pop_heap(first, last, components::DistIndexPair::cmpDist);

			// max element is now on position "back" and should be popped
			// instead we overwrite it directly with the new element
			auto &back = *(last-1);
			back = {distances[i], i};
			std::push_heap(first, last, components::DistIndexPair::cmpDist);
		}
	}
	std::sort_heap(first, last, components::DistIndexPair::cmpDist); // sort ascending
	return ret;
}

}
