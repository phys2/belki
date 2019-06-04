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
		guiState.singleColumn = toggle;
		if (current)
			view->setColumnMode(toggle);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		if (current)
			emit exportRequested(current().scene.get(), "Heatmap");
	});

	/* connect incoming signals */
	connect(this, &Viewer::inUpdateColorset, [this] (auto colors) {
		guiState.colorset = colors;
		if (current)
			current().scene->updateColorset(colors);
	});
	connect(this, &Viewer::inTogglePartitions, [this] (bool show) {
		guiState.showPartitions = show;
		if (current)
			current().scene->togglePartitions(show);
	});
	connect(this, &Viewer::inToggleMarker, [this] (ProteinId id, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarker(id, present);
	});

	/* propagate initial state */
	actionToggleSingleCol->setChecked(guiState.singleColumn);

	updateEnabled();
}

void HeatmapTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto scene
	auto scene = current().scene.get();
	scene->updateColorset(guiState.colorset);
	scene->togglePartitions(guiState.showPartitions);
	scene->updateMarkers();
	view->setScene(scene);
	view->setColumnMode(guiState.singleColumn);
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
