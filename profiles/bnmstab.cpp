#include "bnmstab.h"
#include "bnmschart.h"
#include "compute/features.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>
#include <QAction>
#include <QToolButton>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	auto anchor = actionMarkerMenu;
	toolBar->insertWidget(anchor, proteinBox);
	toolBar->insertSeparator(anchor);

	actionMarkerMenu->setMenu(&markerMenu);
	// let marker button display menu without holding mouse
	auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(actionMarkerMenu));
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);

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
	connect(referenceSelect, qOverload<int>(&QComboBox::currentIndexChanged), [this] {
		if (referenceSelect->currentIndex() < 0)
			return; // nothing selected
		setReference((unsigned)referenceSelect->currentData(Qt::UserRole + 1).value<int>());
	});

	updateEnabled();
}

void BnmsTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	connect(&s->proteins(), &ProteinDB::markersToggled, [this] {
		setupMarkerMenu();
		if (current) // rebuild plot to reflect marker state change (TODO: awkward)
			current().scene->repopulate();
	});
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

	// redundant call as availability of markers is data-dependent
	setupMarkerMenu();
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

	/* connect outgoing signals */
	connect(state.scene.get(), &ProfileChart::menuRequested, [this] (ProteinId id) {
		proteinMenu(id)->exec(QCursor::pos());
	});
}

std::unique_ptr<QMenu> BnmsTab::proteinMenu(ProteinId id)
{
	auto ret = windowState->proteinMenu(id);
	if (ret->actions().count() < 2)
		return ret;
	if (id == tabState.reference)
		return ret;
	auto anchor = ret->actions().at(1);
	auto action = new QAction("Switch to", ret.get());
	connect(action, &QAction::triggered, [this,id] { setReference(id); });
	ret->insertAction(anchor, action);
	return ret;
}

void BnmsTab::setReference(ProteinId id)
{
	tabState.reference = id;
	if (current)
		current().scene->setReference(tabState.reference);
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

void BnmsTab::setupMarkerMenu()
{
	actionMarkerMenu->setEnabled(current);
	if (!current)
		return;

	markerMenu.clear();
	auto p = current().data->peek<Dataset::Proteins>();

	/* we need markers sorted */
	std::vector<ProteinId> markers(p->markers.begin(), p->markers.end());
	std::sort(markers.begin(), markers.end(), [&p] (auto a, auto b) {
		return p->proteins[a].name < p->proteins[b].name;
	});

	auto selector = [this] (ProteinId id) {
		auto index = referenceSelect->findData(id, Qt::UserRole + 1);
		referenceSelect->setCurrentIndex(index);
	};

	auto b = current().data->peek<Dataset::Base>();
	for (auto protId : markers) {
		if (!b->protIndex.count(protId))
			continue;
		markerMenu.addAction(p->proteins[protId].name,
		                     [selector,protId] { selector(protId); });
	}

	actionMarkerMenu->setEnabled(!markerMenu.isEmpty());
}
