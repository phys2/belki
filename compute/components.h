#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "model.h"
#include "profiles/bnmsmodel.h" // TODO: awkward, see comment in that file

#include <QObject>
#include <QThread>
#include <tbb/task.h>
#include <memory>

class Dataset; // TODO: bad style, maybe Matcher should not be in compute/..

namespace components {

std::pair<size_t, size_t> gauss_cover(double mean, double sigma, size_t range, double factor=3.5);

std::vector<double> generate_gauss(size_t range, double mean, double sigma, double scale=1.);
void add_gauss(std::vector<double> &target, double mean, double sigma, double scale=1.);

struct DistIndexPair {
	DistIndexPair()
		: dist(std::numeric_limits<double>::infinity()), index(0)
	{}
	DistIndexPair(double dist, size_t index)
		: dist(dist), index(index)
	{}

	/** Compare function to sort by distance. */
	static inline bool cmpDist(const DistIndexPair& a, const DistIndexPair& b)
	{
		return (a.dist < b.dist);
	}

	double dist;
	size_t index;
};

class Matcher : public QObject {
	Q_OBJECT

public:
	Matcher(std::shared_ptr<Dataset const> data, const std::vector<Components> &comps);

	static std::vector<DistIndexPair> rank(const std::vector<double> &distances, size_t topN, unsigned ignore);

public slots:
	void matchRange(unsigned reference, std::pair<double, double> range, unsigned topN);
	void matchComponents(Components reference, unsigned topN, unsigned ignore);

signals:
	void newRanking(std::vector<components::DistIndexPair> top);

protected:
	void compute();

	struct {
		unsigned topN;
		unsigned reference;

		std::pair<double, double> range;
		Components refComponents;
	} config;

	std::shared_ptr<Dataset const> data;
	const std::vector<Components> &comps;
};

}
Q_DECLARE_METATYPE(components::DistIndexPair)

#endif
