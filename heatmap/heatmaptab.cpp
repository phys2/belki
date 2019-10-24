#include "heatmaptab.h"
#include "heatmapscene.h"

HeatmapTab::HeatmapTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	setupOrderUI();

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* connect toolbar actions */
	connect(actionToggleSingleCol, &QAction::toggled, [this] (bool toggle) {
		tabState.singleColumn = toggle;
		if (current)
			view->setColumnMode(toggle);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		if (current)
			emit exportRequested(current().scene.get(), "Heatmap");
	});

	/* connect incoming signals */
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarkers(ids, present);
	});

	/* propagate initial state */
	actionToggleSingleCol->setChecked(tabState.singleColumn);

	updateEnabled();
}

void HeatmapTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);

	/* connect state change signals */
	auto ws = s.get();
	connect(ws, &WindowState::annotationsToggled, [this] () {
		if (current)
			current().scene->togglePartitions(windowState->showAnnotations);
	});
}

void HeatmapTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto scene
	auto scene = current().scene.get();
	scene->togglePartitions(windowState->showAnnotations);
	scene->updateMarkers();
	view->setScene(scene);
	view->setColumnMode(tabState.singleColumn);
}

void HeatmapTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<HeatmapScene>(data);

	auto scene = state.scene.get();

	/* connect outgoing signals */
	connect(scene, &HeatmapScene::cursorChanged, this, &Viewer::cursorChanged);
}

/* Note: shared code between DistmatTab and HeatmapTab */
void HeatmapTab::setupOrderUI()
{
	// setup toolbar
	auto anchor = actionLockOrder;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, orderLabel);
	toolBar->insertWidget(anchor, orderSelect);

	// fill-up order select
	for (auto &[v, n] : Dataset::availableOrders())
		orderSelect->addItem(n, QVariant::fromValue(v));
	orderSelect->setCurrentText(Dataset::availableOrders().at(Dataset::OrderBy::HIERARCHY));

	// signalling
	auto cO = [this] {
		emit orderRequested(orderSelect->currentData().value<Dataset::OrderBy>(),
		                 !actionLockOrder->isChecked()); // lock → sync
	};
	connect(orderSelect, QOverload<int>::of(&QComboBox::activated), cO);
	connect(actionLockOrder, &QAction::toggled, cO);
	connect(this, &Viewer::changeOrder, [this] (auto order, bool sync) {
		const QSignalBlocker a(orderSelect), b(actionLockOrder);
		orderSelect->setCurrentText(Dataset::availableOrders().at(order));
		actionLockOrder->setChecked(!sync); // sync → lock
	});

	// remove container we picked from
	orderBar->deleteLater();
}

void HeatmapTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
