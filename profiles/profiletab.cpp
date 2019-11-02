#include "profiletab.h"
#include "profilechart.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>

ProfileTab::ProfileTab(QWidget *parent) :
    Viewer(parent), proteinModel(tabState.extras)
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

	/* connect incoming signals */
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			rebuildPlot(); // TODO temporary hack
	});

	updateEnabled();
}

void ProfileTab::setProteinModel(QAbstractItemModel *m)
{
	proteinModel.setSourceModel(m);
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
	scene->toggleLabels(tabState.showLabels);
	scene->toggleAverage(tabState.showAverage);
	scene->toggleQuantiles(tabState.showQuantiles);

	// apply datastate
	actionLogarithmic->setChecked(current().logSpace);

	view->setChart(scene);
}

void ProfileTab::addDataset(Dataset::Ptr data)
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

bool ProfileTab::eventFilter(QObject *watched, QEvent *event)
{
	auto ret = Viewer::eventFilter(watched, event);

	/* open completer popup when clicking on protsearch line edit */
	if (watched == protSearch && event->type() == QEvent::MouseButtonPress) {
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
			protSearch->completer()->complete();
	}

	return ret;
}

void ProfileTab::rebuildPlot()
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

void ProfileTab::setupProteinBox()
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

	auto toggler = [this] (QModelIndex i) {
		if (!i.isValid())
			return; // didn't click on a row, e.g. clicked on a checkmark
		auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
		if (!proxy)
			return; // sorry, can't do this!
		auto id = unsigned(proteinModel.data(proxy->mapToSource(i), Qt::UserRole + 1).toInt());
		if (tabState.extras.count(id))
			tabState.extras.erase(id);
		else
			tabState.extras.insert(id);
		rebuildPlot();
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

void ProfileTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

QVariant ProfileTab::CustomCheckedProxyModel::data(const QModelIndex &index, int role) const
{
	if (role != Qt::CheckStateRole)
		return QIdentityProxyModel::data(index, role);

	if (marked.count((unsigned)data(index, Qt::UserRole + 1).toInt()))
		return Qt::Checked;

	// use "partially checked" to pass-on marker state
	auto d = sourceModel()->data(mapToSource(index), role);
	return (d == Qt::Checked ? Qt::PartiallyChecked : d);
}
