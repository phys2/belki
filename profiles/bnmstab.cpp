#include "bnmstab.h"
#include "bnmschart.h"
#include "rangeselectitem.h"
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
	auto anchor = actionHistoryMenu;
	toolBar->insertWidget(anchor, proteinBox);
	toolBar->insertSeparator(anchor);

	actionHistoryMenu->setMenu(&historyMenu);
	actionHistoryMenu->setEnabled(false); // no history yet
	actionMarkerMenu->setMenu(&markerMenu);
	// buttons display menu without holding mouse
	for (auto i : {actionHistoryMenu, actionMarkerMenu}) {
		auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(i));
		btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
	}

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
			current().refScene->toggleLogSpace(on);
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
	auto refScene = current().refScene.get();
	refScene->clear();
	refScene->addSample(tabState.reference, true);
	refScene->finalize();

	// apply datastate
	actionLogarithmic->setChecked(current().logSpace);

	view->setChart(scene);
	referenceView->setChart(refScene);

	// redundant call as availability of markers is data-dependent
	setupMarkerMenu();
}

void BnmsTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<BnmsChart>(data);
	state.refScene = std::make_unique<ProfileChart>(data, false, false);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
		state.refScene->toggleLogSpace(true);
	}

	/* setup range */
	auto ndim = data->peek<Dataset::Base>()->dimensions.size();
	auto rangeItem = std::make_unique<RangeSelectItem>(state.refScene.get());
	rangeItem->setLimits(0, ndim);
	rangeItem->setRange(0, ndim);
	connect(rangeItem.get(), &RangeSelectItem::borderChanged,
	        state.scene.get(), &BnmsChart::setBorder);
	state.rangeSelect = std::move(rangeItem);
	state.scene->setBorder(Qt::Edge::RightEdge, ndim);

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
	auto action = new QAction(QIcon::fromTheme("go-next"), "Set as reference", ret.get());
	connect(action, &QAction::triggered, [this,id] { setReference(id); });
	ret->insertAction(anchor, action);
	return ret;
}

void BnmsTab::setReference(ProteinId id)
{
	if (tabState.reference == id)
		return;

	addToHistory(tabState.reference);
	tabState.reference = id;
	if (current) {
		current().scene->setReference(tabState.reference);
		auto &refScene = current().refScene;
		refScene->clear();
		refScene->addSample(tabState.reference, true);
		refScene->finalize();
	}

	QSignalBlocker _(referenceSelect);
	referenceSelect->setCurrentIndex(referenceSelect->findData(id, Qt::UserRole + 1));
}

void BnmsTab::addToHistory(ProteinId id)
{
	actionHistoryMenu->setEnabled(true);
	auto name = windowState->proteins().peek()->proteins[id].name;
	auto action = new QAction(name, &historyMenu);
	connect(action, &QAction::triggered, [this,id] { setReference(id); });
	if (historyMenu.isEmpty()) {
		historyMenu.addAction(action);
		return;
	}

	const auto &entries = historyMenu.actions();
	historyMenu.insertAction(entries.first(), action);
	if (entries.count() > 20)
		historyMenu.removeAction(entries.last());
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

	auto b = current().data->peek<Dataset::Base>();
	for (auto protId : markers) {
		if (!b->protIndex.count(protId))
			continue;
		markerMenu.addAction(p->proteins[protId].name,
		                     [this,protId] { setReference(protId); });
	}

	actionMarkerMenu->setEnabled(!markerMenu.isEmpty());
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

BnmsTab::DataState::~DataState()
{
}
