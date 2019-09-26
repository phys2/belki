#include "annotations.h"
#include "utils.h"

#include "meanshift/fams.h"

#include <QThread>
#include <QCollator>
#include <iostream>
#include <unordered_set>

namespace annotations {

void order(Annotations &data, bool genericNames)
{
	auto &gr = data.groups;
	auto &target = data.order;
	target.clear();
	for (auto & [i, _] : gr)
		target.push_back(i);

	QCollator col;
	col.setNumericMode(true);
	if (col("a", "a")) {
		std::cerr << "Falling back to non-numeric sorting." << std::endl;
		col.setNumericMode(false);
	}

	col.setCaseSensitivity(Qt::CaseInsensitive);
	std::function<bool(unsigned,unsigned)> byName = [&] (auto a, auto b) {
		return col(gr.at(a).name, gr.at(b).name);
	};
	auto bySizeName = [&] (auto a, auto b) {
		auto sizea = gr.at(a).members.size(), sizeb = gr.at(b).members.size();
		if (sizea == sizeb)
			return byName(a, b);
		return sizea > sizeb;
	};

	std::sort(target.begin(), target.end(), genericNames ? bySizeName : byName);
}

void color(Annotations &data, const QVector<QColor> &colors)
{
	for (unsigned i = 0; i < data.groups.size(); ++i) {
		data.groups[data.order[i]].color = colors.at((int)i % colors.size());
	}
}

void prune(Annotations &data)
{
	float total = 0;
	for (auto [_,v] : data.groups)
		total += v.members.size();

	// TODO: make configurable; instead keep X biggest clusters?
	auto minSize = unsigned(0.005f * total);
	erase_if(data.groups, [minSize] (auto it) {
		return it->second.members.size() < minSize;
	});
}

Annotations partition(const HrClustering &in, unsigned granularity)
{
	auto &hrclusters = in.clusters;

	granularity = std::min(granularity, (unsigned)hrclusters.size());
	unsigned lowBound = hrclusters.size() - granularity - 1;

	/* determine clusters to be displayed */
	std::unordered_set<unsigned> candidates;
	// we use the fact that input is sorted by distance, ascending
	for (auto i = lowBound; i < hrclusters.size(); ++i) {
		auto &current = hrclusters[i];

		// add either parent or childs, if any of them is eligible by-itself
		auto useChildrenInstead =
		        std::any_of(current.children.begin(), current.children.end(),
		                    [lowBound] (auto c) { return c >= lowBound; });
		if (useChildrenInstead) {
			for (auto c : current.children) {
				if (c < lowBound) // only add what's not covered by granularity
					candidates.insert(c);
			}
		} else {
			candidates.insert(i);
		}
	}

	Annotations ret;

	// helper to recursively assign all proteins to clusters
	std::function<void(unsigned, unsigned)> flood;
	flood = [&] (unsigned hIndex, unsigned cIndex) {
		auto &current = hrclusters[hIndex];
		if (current.protein)
			// would use std::optional::value(), but not available on MacOS 10.13
			ret.groups.at(cIndex).members.push_back(current.protein.value_or(0));
		for (auto c : current.children)
			flood(c, cIndex);
	};

	ret.groups.reserve(candidates.size());
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(hrclusters.size() - i);
		// use index in hierarchy as cluster index as well
		ret.groups[i].name = name; // initializes
		flood(i, i);
	}

	ret.name = in.name + QString(" (granularity %1)").arg(granularity);
	return ret;
}

Meanshift::Meanshift(const Features::Vec &input)
    : fams(new seg_meanshift::FAMS({.pruneMinN = 0}))
{
	std::scoped_lock _(l);
	fams->importPoints(input, true); // scales vectors
	fams->selectStartPoints(0., 1); // perform for all features
}

Meanshift::~Meanshift()
{
	cancel();
	std::scoped_lock _(l); // wait for cancel
}

std::optional<Meanshift::Result> Meanshift::applyK(float newK)
{
	if (k == newK)
		return {};

	k = newK;
	fams->cancel();
	return compute();
}

void Meanshift::cancel()
{
	k = 0;
	fams->cancel(); // note: asynchronous, non-blocking for us
}

std::optional<Meanshift::Result> Meanshift::compute()
{
	std::scoped_lock _(l); // wait for any other threads to finish

	/* We compress several redundant requests for computation by always computing
	 * on the latest setting for k and by not repeating computation if same k was
	 * used in the previous run. */
	if (fams->config.k == k || k == 0)
		return {};

	fams->resetState();
	fams->config.k = k;

	bool success = fams->prepareFAMS();
	if (!success) {	// cancelled
		return {};
	}

	success = fams->finishFAMS();
	if (!success) {	// cancelled
		return {};
	}

	fams->pruneModes();
	return {{fams->exportModes(), fams->getModePerPoint()}};
}

}
