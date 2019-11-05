#include "bnmschart.h"
#include "dataset.h"
#include "compute/features.h"

#include <QtCharts/QLineSeries>

/* small, inset plot constructor */
BnmsChart::BnmsChart(Dataset::ConstPtr dataset)
    : ProfileChart(dataset, false, true)
{
	// we provide sorted by distance
	sort = Sorting::NONE;
}

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

void BnmsChart::setReference(ProteinId ref)
{
	auto b = data->peek<Dataset::Base>();
	auto r = b->protIndex.find(ref);
	if (r == b->protIndex.end()) {
		clear(); // invalid reference for our dataset
		return;
	}
	if (reference == r->second)
		return;

	reference = r->second;

	/* fun with knives */
	const unsigned numProts = 10; // TODO: dynamic based on relative offset?

	auto distance = features::distfun(features::Distance::COSINE);
	auto heapsOfFun = [&] {
		std::vector<DistIndexPair> ret(numProts);
		auto dfirst = ret.begin(), dlast = ret.end();
		// initialize heap with infinity distances
		std::fill(dfirst, dlast, DistIndexPair());

		for (size_t i = 0; i < b->features.size(); ++i) {
			if (i == r->second)
				continue;

			auto dist = distance(b->features[i], b->features[r->second]);
			if (dist < dfirst->dist) {
				// remove max. value in heap
				std::pop_heap(dfirst, dlast, DistIndexPair::cmpDist);

				// max element is now on position "back" and should be popped
				// instead we overwrite it directly with the new element
				DistIndexPair &back = *(dlast-1);
				back = DistIndexPair(dist, i);
				std::push_heap(dfirst, dlast, DistIndexPair::cmpDist);
			}
		}
		std::sort_heap(dfirst, dlast, DistIndexPair::cmpDist); // sort ascending
		return ret;
	};

	clear();
	addSampleByIndex(reference, true);
	auto candidates = heapsOfFun();
	for (auto c : candidates)
		addSampleByIndex(c.index, false);
	finalize();
}

void BnmsChart::animHighlight(int index, qreal step)
{
	/* same as ProfileChart::animHighlight(), but we always let reference stand
	 * and also hide other profiles even more */
	bool decrease = (index < 0);
	bool done = true;
	for (auto &[i, s] : series) {
		auto c = s->color();
		if ((int)i == index || i == reference || decrease) { // ⟵
			if (c.alphaF() < 1.) {
				c.setAlphaF(std::min(1., c.alphaF() + step));
				done = false;
			}
		} else {
			if (c.alphaF() > .1) {
				c.setAlphaF(std::max(.1, c.alphaF() - step));
				done = false;
			}
		}
		s->setColor(c);
	}
	if (done)
		highlightAnim.stop();
}
