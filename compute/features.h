#ifndef FEATURES_H
#define FEATURES_H

#include <QVector>
#include <QMap>
#include <QPointF>
#include <vector>

namespace features {

using vec = std::vector<std::vector<double>>;

struct Range {
	explicit Range(const vec& source);
	Range() = default;
	Range(double min, double max) : min(min), max(max) {}

	double scale() const;

	double min;
	double max;
};

unsigned cutoff_effect(const vec& source, double threshold);
// apply threshold on scores (_upper_ limit) by erasing corresp. features
void apply_cutoff(vec& feats, const vec &scores, double threshold);

std::vector<QVector<QPointF>> pointify(const vec &source);
QVector<QPointF> scatter(const vec &x, size_t xi, const vec &y, size_t yi);

}

#endif
