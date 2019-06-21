#ifndef FEATURES_H
#define FEATURES_H

#include "model.h"

namespace features {

using vec = Features::Vec;

Features::Range range_of(const vec &source);
unsigned cutoff_effect(const vec& source, double threshold);
// apply threshold on scores (_upper_ limit) by erasing corresp. features
void apply_cutoff(vec& feats, const vec &scores, double threshold);

std::vector<QVector<QPointF>> pointify(const vec &source);
QVector<QPointF> scatter(const vec &x, size_t xi, const vec &y, size_t yi);

}

#endif
