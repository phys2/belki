#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "model.h"

namespace components {

std::pair<size_t, size_t> gauss_cover(double mean, double sigma, size_t range, double factor=3.5);

std::vector<double> generate_gauss(size_t range, double mean, double sigma, double scale=1.);
void add_gauss(std::vector<double> &target, double mean, double sigma, double scale=1.);

}

#endif
