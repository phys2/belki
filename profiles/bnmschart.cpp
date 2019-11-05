#include "bnmschart.h"
#include "rangeselectitem.h"
#include "dataset.h"
#include "compute/features.h"
#include "compute/colors.h"

#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

/* small, inset plot constructor */
BnmsChart::BnmsChart(Dataset::ConstPtr dataset)
    : ProfileChart(dataset, false, true)
{
	// we provide sorted by distance
	sort = Sorting::NONE;

	auto ndim = dataset->peek<Dataset::Base>()->dimensions.size();
	range = {0, ndim};
	rangeItem = std::make_unique<RangeSelectItem>(this);
	rangeItem->setLimits(0, ndim);
	rangeItem->setRange(0, ndim);
	connect(this, &BnmsChart::plotAreaChanged,
	        rangeItem.get(), &RangeSelectItem::setRect);
	connect(rangeItem.get(), &RangeSelectItem::borderChanged, this,
	        [this] (RangeSelectItem::Border border, qreal value) {
		if (border == RangeSelectItem::LEFT)
			range.first = value;
		else
			range.second = value;
		repopulate();
	});
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

BnmsChart::~BnmsChart()
{
	// needed to delete unique_ptr
}

void BnmsChart::clear()
{
	scores.clear();
	ProfileChart::clear();
}

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
	repopulate();
};

void BnmsChart::repopulate()
{
	const unsigned numProts = 10; // TODO: dynamic based on relative offset?
	auto distance = features::distfun(features::Distance::COSINE);

	auto b = data->peek<Dataset::Base>();
	meanScore = 0.;
	std::vector<DistIndexPair> candidates(numProts);
	auto dfirst = candidates.begin(), dlast = candidates.end();
	// initialize heap with infinity distances
	std::fill(dfirst, dlast, DistIndexPair());

	std::vector<double> r(b->features[reference].begin() + (int)range.first,
	                      b->features[reference].begin() + (int)range.second);
	for (size_t i = 0; i < b->features.size(); ++i) {
		if (i == reference)
			continue;

		std::vector<double> f(b->features[i].begin() + (int)range.first,
		                      b->features[i].begin() + (int)range.second);
		auto dist = distance(f, r);
		if (dist < dfirst->dist) {
			// remove max. value in heap
			std::pop_heap(dfirst, dlast, DistIndexPair::cmpDist);

			// max element is now on position "back" and should be popped
			// instead we overwrite it directly with the new element
			DistIndexPair &back = *(dlast-1);
			back = DistIndexPair(dist, i);
			std::push_heap(dfirst, dlast, DistIndexPair::cmpDist);
		}
		meanScore += dist;
	}
	std::sort_heap(dfirst, dlast, DistIndexPair::cmpDist); // sort ascending
	meanScore /= b->features.size();

	clear();
	addSampleByIndex(reference, true);
	for (auto c : candidates) {
		scores[c.index] = c.dist;
		addSampleByIndex(c.index, false);
	}
	finalize();
}

QString BnmsChart::titleOf(unsigned int index, const QString &name, bool isMarker) const
{
	if (index == reference)
		return QString("<b>%1</b>").arg(name);

	auto score = scores.at(index);
	auto color = Colormap::qcolor(Colormap::stoplight.apply(-score, -1., 0.));
	return QString("%1 <small style='background-color: %3; color: black;'>%2</small>")
	        .arg(name).arg(score, 4, 'f', 3).arg(color.name());
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
