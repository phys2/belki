#include "bnmstab.h"
#include "profilechart.h"
#include "compute/features.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	auto anchor = actionShowLabels;
	toolBar->insertWidget(anchor, proteinBox);
	toolBar->insertSeparator(anchor);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* connect toolbar actions */
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Selected Profiles");
	});
	connect(actionShowLabels, &QAction::toggled, [this] (bool on) {
		tabState.showLabels = on;
		if (current) current().scene->toggleLabels(on);
	});
	connect(actionShowAverage, &QAction::toggled, [this] (bool on) {
		tabState.showAverage = on;
		if (current) current().scene->toggleAverage(on);
	});
	connect(actionShowQuantiles, &QAction::toggled, [this] (bool on) {
		tabState.showQuantiles = on;
		if (current) current().scene->toggleQuantiles(on);
	});
	connect(actionShowIndividual, &QAction::toggled, [this] (bool on) {
		if (current) current().scene->toggleIndividual(on);
	});
	connect(actionLogarithmic, &QAction::toggled, [this] (bool on) {
		if (current) {
			current().logSpace = on;
			current().scene->toggleLogSpace(on);
		}
	});
	connect(referenceSelect, qOverload<int>(&QComboBox::activated), [this] {
		tabState.reference = referenceSelect->currentData(Qt::UserRole + 1).value<int>();
		if (current)
			rebuildPlot();
	});

	updateEnabled();
}

void BnmsTab::setProteinModel(QAbstractItemModel *m)
{
	referenceSelect->setModel(m);
}

void BnmsTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto chart
	auto scene = current().scene.get();
	rebuildPlot();  // TODO temporary hack
	scene->toggleLabels(tabState.showLabels);
	scene->toggleAverage(tabState.showAverage);
	scene->toggleQuantiles(tabState.showQuantiles);

	// apply datastate
	actionLogarithmic->setChecked(current().logSpace);

	view->setChart(scene);
}

void BnmsTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<ProfileChart>(data, false, true);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
	}
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

void BnmsTab::rebuildPlot()
{
	auto scene = current().scene.get();

	scene->clear();
	scene->addSample(tabState.reference, true);
	/* fun with knives */
	auto b = current().data->peek<Dataset::Base>();
	auto r = b->protIndex.find(tabState.reference);
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

	if (r != b->protIndex.end()) {
		auto candidates = heapsOfFun();
		for (auto c : candidates)
			scene->addSample(c.index, false);
	}
	scene->finalize();
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
