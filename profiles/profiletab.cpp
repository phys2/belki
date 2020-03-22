#include "profiletab.h"
#include "profilechart.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>
#include <QMenu>
#include <QCursor>
#include <QShortcut>

ProfileTab::ProfileTab(QWidget *parent) :
    Viewer(parent), proteinModel(tabState.extras)
{
	setupUi(this);
	view->setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5
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
		if (haveData()) selected().scene->toggleLabels(on);
	});
	connect(actionShowAverage, &QAction::toggled, [this] (bool on) {
		tabState.showAverage = on;
		if (haveData()) selected().scene->toggleAverage(on);
	});
	connect(actionShowQuantiles, &QAction::toggled, [this] (bool on) {
		tabState.showQuantiles = on;
		if (haveData()) selected().scene->toggleQuantiles(on);
	});
	connect(actionShowIndividual, &QAction::toggled, [this] (bool on) {
		if (haveData()) selected().scene->toggleIndividual(on);
	});
	connect(actionLogarithmic, &QAction::toggled, [this] (bool on) {
		if (haveData()) {
			selected().logSpace = on;
			selected().scene->toggleLogSpace(on);
		}
	});

	/* have a handy shortcut */
	auto zoomReset = new QShortcut(this);
	zoomReset->setKey({"Shift+z"});
	connect(zoomReset, &QShortcut::activated, [this] {
		if (haveData())
			selected().scene->zoomReset();
	});

	updateIsEnabled();
}

ProfileTab::~ProfileTab()
{
	deselectDataset(); // avoid double delete
}

void ProfileTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	connect(&s->proteins(), &ProteinDB::markersToggled, this, [this] {
		rebuildPlot();
	});
}

void ProfileTab::setProteinModel(QAbstractItemModel *m)
{
	proteinModel.setSourceModel(m);
}

void ProfileTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	// pass guiState onto chart
	auto scene = selected().scene.get();
	rebuildPlot();  // TODO temporary hack
	scene->toggleLabels(tabState.showLabels);
	scene->toggleAverage(tabState.showAverage);
	scene->toggleQuantiles(tabState.showQuantiles);

	// apply datastate
	actionLogarithmic->setChecked(selected().logSpace);

	view->setChart(scene);
}

void ProfileTab::deselectDataset()
{
	view->setChart(new QtCharts::QChart()); // release ownership
	Viewer::deselectDataset();
}

void ProfileTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.scene = std::make_unique<ProfileChart>(data, false, true);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
	}

	/* connect outgoing signals */
	connect(state.scene.get(), &ProfileChart::menuRequested, [this] (ProteinId id) {
		proteinMenu(id)->exec(QCursor::pos());
	});
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

std::unique_ptr<QMenu> ProfileTab::proteinMenu(ProteinId id)
{
	auto ret = windowState->proteinMenu(id);
	if (ret->actions().count() < 2)
		return ret;
	if (windowState->proteins().peek()->markers.count(id))
		return ret; // don't confuse user with markers shadowing extra proteins
	auto anchor = ret->actions().at(1);
	auto text = (tabState.extras.count(id) ? "Remove from plot" : "Add to plot");
	auto action = new QAction(text, ret.get());
	connect(action, &QAction::triggered, [this,id] { toggleExtra(id); });
	ret->insertAction(anchor, action);
	return ret;
}

void ProfileTab::rebuildPlot()
{
	if (!haveData())
		return;

	auto scene = selected().scene.get();
	scene->clear();
	auto markers = selected().data->peek<Dataset::Proteins>()->markers; // copy
	for (auto m : markers)
		scene->addSample(m, true);
	for (auto e : tabState.extras) {
		if (!markers.count(e))
			scene->addSample(e, false);
	}
	scene->finalize();
}

void ProfileTab::toggleExtra(ProteinId id)
{
	if (tabState.extras.count(id))
		tabState.extras.erase(id);
	else
		tabState.extras.insert(id);
	rebuildPlot();
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
		toggleExtra(id);
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

bool ProfileTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	setEnabled(on);
	view->setVisible(on);
	return on;
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
