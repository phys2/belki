#ifndef FEATURES_H
#define FEATURES_H

#include "model.h"

namespace features {

using vec = Features::Vec;

// compute range that includes at least <fraction> of the observed values
Features::Range range_of(const vec &source, float fraction = 1.f);
Features::Range log_valid(const Features::Range &range);
unsigned cutoff_effect(const vec& source, double threshold);

// apply threshold on scores (_upper_ limit) by erasing corresp. features
vec with_cutoff(const vec& feats, const vec &scores, double threshold);
// version that also alters scores to reflect new limit
void apply_cutoff(vec& feats, vec &scores, double threshold);

std::vector<QVector<QPointF>> pointify(const vec &source);
QVector<QPointF> scatter(const vec &x, size_t xi, const vec &y, size_t yi);

enum class Distance {
	EUCLIDEAN,
	COSINE,
	CROSSCORREL, // note: higher is better
	PEARSON, // note: higher is better
	EMD, // Earth Mover's Distance
};

template<Distance D>
double distance(const std::vector<double> &a, const std::vector<double> &b);

std::function<double(const std::vector<double> &a, const std::vector<double> &b)>
distfun(Distance measure);

}

#endif