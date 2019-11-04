#include "bnmstab.h"
#include "profilechart.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	setupProteinBox();
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

	updateEnabled();
}

void BnmsTab::setProteinModel(QAbstractItemModel *m)
{
	proteinModel.setSourceModel(m);
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

bool BnmsTab::eventFilter(QObject *watched, QEvent *event)
{
	auto ret = Viewer::eventFilter(watched, event);

	/* open completer popup when clicking on protsearch line edit */
	if (watched == protSearch && event->type() == QEvent::MouseButtonPress) {
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
			protSearch->completer()->complete();
	}

	return ret;
}

void BnmsTab::rebuildPlot()
{
	auto scene = current().scene.get();

	scene->clear();
	auto markers = current().data->peek<Dataset::Proteins>()->markers; // copy
	for (auto m : markers)
		scene->addSample(m, true);
	for (auto e : tabState.extras) {
		if (!markers.count(e))
			scene->addSample(e, false);
	}
	scene->finalize();
}

void BnmsTab::setupProteinBox()
{
	/* setup completer with empty model */
	auto m = &proteinModel;
	auto cpl = new QCompleter(m, this);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	// we expect model entries to be sorted
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	cpl->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
	cpl->setMaxVisibleItems(10);
	protSearch->setCompleter(cpl);

	// let us watch out for clicks
	protSearch->installEventFilter(this);
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

Qt::ItemFlags BnmsTab::RemoveCheckstateProxyModel::flags(const QModelIndex &index) const
{
	auto flags = sourceModel()->flags(mapToSource(index));
	flags.setFlag(Qt::ItemFlag::ItemIsUserCheckable, false);
	return flags;
}
