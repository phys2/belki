#include "components.h"

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

}
