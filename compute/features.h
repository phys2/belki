#ifndef FEATURES_H
#define FEATURES_H

#include <QVector>
#include <QMap>
#include <QPointF>
#include <vector>

namespace features {

using featvec = std::vector<std::vector<double>>;

unsigned cutoff_effect(const featvec& source, double threshold);
// apply threshold on scores (_upper_ limit) by erasing corresp. features
void apply_cutoff(featvec& feats, const featvec &scores, double threshold);

}

#endif
