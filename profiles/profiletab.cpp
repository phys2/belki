#include "profiletab.h"
#include "profilechart.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>

ProfileTab::ProfileTab(QWidget *parent) :
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
		guiState.showLabels = on;
		if (current) current().scene->toggleLabels(on);
	});
	connect(actionShowAverage, &QAction::toggled, [this] (bool on) {
		if (current) current().scene->toggleAverage(on);
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

	/* connect incoming signals */
	connect(this, &Viewer::inAddProtein, this, &ProfileTab::addProtein);
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			rebuildPlot(); // TODO temporary hack
	});

	updateEnabled();
}

void ProfileTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto chart
	auto scene = current().scene.get();
	rebuildPlot();  // TODO temporary hack
	scene->toggleLabels(guiState.showLabels);
	updateProteinItems();

	// apply datastate
	actionLogarithmic->setChecked(current().logSpace);

	view->setChart(scene);
}

void ProfileTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<ProfileChart>(data, false);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
	}

	/* connect outgoing signals */
	// auto scene = state.scene.get();
	// none right now
}

void ProfileTab::rebuildPlot()
{
	auto scene = current().scene.get();

	scene->clear();
	auto markers = current().data->peek<Dataset::Proteins>()->markers; // copy
	for (auto m : markers)
		scene->addSample(m, true);
	for (auto e : guiState.extras) {
		if (!markers.count(e))
			scene->addSample(e, false);
	}
	scene->finalize();
	actionShowAverage->setEnabled(scene->numProfiles() >= 2);
}

void ProfileTab::addProtein(ProteinId id, const Protein &protein)
{
	/* setup new item */
	auto item = new QStandardItem;
	item->setText(protein.name);
	item->setData(id);
	item->setCheckable(true);
	item->setCheckState(Qt::Unchecked);
	// item->setEnabled(false); // would be great, but seems the later flip is expensive 😟

	/* add item to model */
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->appendRow(item);
	proteinItems[id] = item;

	/* ensure items are sorted in the end, but defer sorting */
	proteinBox->setEnabled(false);
	proteinModelDirty = true;
	QTimer::singleShot(0, this, &ProfileTab::finalizeProteinBox);
}

void ProfileTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

void ProfileTab::setupProteinBox()
{
	/* setup completer with empty model */
	auto m = new QStandardItemModel(this);
	auto cpl = new QCompleter(m, this);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	// we expect model entries to be sorted
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	cpl->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
	cpl->setMaxVisibleItems(10);
	protSearch->setCompleter(cpl);

	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		auto id = ProteinId(i->data().toInt());
		bool wanted = (i->checkState() == Qt::Checked);
		if (wanted == guiState.extras.count(id)) // check state did not change
			return;
		if (wanted)
			guiState.extras.insert(id);
		else
			guiState.extras.erase(id);
		rebuildPlot(); // TODO temporary hack
	});

	auto toggler = [m] (QModelIndex i) {
		if (!i.isValid())
			return; // didn't click on a row, e.g. clicked on a checkmark
		auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
		if (!proxy)
			return; // sorry, can't do this!
		auto item = m->itemFromIndex(proxy->mapToSource(i));
		if (!item->isEnabled())
			return;
		item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
	};

	/* Allow to toggle check state by click */
	connect(cpl, qOverload<const QModelIndex &>(&QCompleter::activated), toggler);

	/* Allow to toggle by pressing <Enter> in protSearch
	   Note: this interferes with the completer's own <Enter> handling.. */
	/*connect(protSearch, &QLineEdit::returnPressed, [this, cpl, toggler] {
		if (cpl->currentCompletion() == protSearch->text()) // still relevant
			toggler(cpl->currentIndex());
	});*/
}

void ProfileTab::finalizeProteinBox()
{
	if (!proteinModelDirty) // already in good state
		return;

	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->sort(0);
	proteinModelDirty = false;
	proteinBox->setEnabled(true); // we are in good state now
}

void ProfileTab::updateProteinItems()
{
	if (current) {
		auto d = current().data->peek<Dataset::Base>();
		for (auto& [id, item] : proteinItems)
			item->setEnabled(d->protIndex.count(id));
	} else {
		for (auto& [id, item] : proteinItems)
			item->setEnabled(false);
	}
}
