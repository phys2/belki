#include "bnmstab.h"
#include "bnmschart.h"
#include "compute/features.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	auto anchor = actionMarkerMenu;
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
		auto value = referenceSelect->currentData(Qt::UserRole + 1).value<int>();
		tabState.reference = (unsigned)value;
		if (current)
			current().scene->setReference(tabState.reference);
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
	scene->setReference(tabState.reference);
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
	state.scene = std::make_unique<BnmsChart>(data);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
	}
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
