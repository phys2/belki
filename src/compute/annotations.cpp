#include "annotations.h"
#include "utils.h"

#include "meanshift/fams.h"

#include <QCollator>
#include <iostream>

namespace annotations {

bool equal(const Annotations::Meta &a, const Annotations::Meta &b)
{
	if (a.id != b.id) // compare stored meta only by id as it should be projectwide-unique
		return false;
	if (a.id == 0) {
		// special cases, non-stored, go through type & settings
		if (a.type != b.type)
			return false;
		if (a.pruned != b.pruned) // currently shared by all types (MS, HIERCUT)
			return false;
		if (a.type == Annotations::Meta::MEANSHIFT) {
			if (a.k != b.k)
				return false;
		} else if (a.type == Annotations::Meta::HIERCUT) {
			if (a.hierarchy != b.hierarchy)
				return false;
			if (a.granularity != b.granularity)
				return false;
		}
	}
	return true;
}

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

std::optional<Meanshift::Result> Meanshift::run(float k)
{
	fams->cancel();
	std::scoped_lock _(l); // wait for any other threads to finish

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

void Meanshift::cancel()
{
	fams->cancel(); // note: asynchronous, non-blocking for us
}

}
