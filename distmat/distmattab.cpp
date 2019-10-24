#include "distmattab.h"
#include "distmatscene.h"

DistmatTab::DistmatTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	setupOrderUI();

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* connect toolbar actions */
	connect(actionToggleDistdir, &QAction::toggled, [this] (bool toggle) {
		tabState.direction = toggle ? Dataset::Direction::PER_DIMENSION
		                            : Dataset::Direction::PER_PROTEIN;
		if (current)
			current().scene->setDirection(tabState.direction);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	/* connect incoming signals */
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarkers(ids, present);
	});

	/* propagate initial state */
	actionToggleDistdir->setChecked(tabState.direction == Dataset::Direction::PER_DIMENSION);

	updateEnabled();
}

void DistmatTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);

	/* connect state change signals */
	auto ws = s.get();
	connect(ws, &WindowState::annotationsToggled, [this] () {
		if (current)
			current().scene->togglePartitions(windowState->showAnnotations);
	});
}

void DistmatTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto chart
	auto scene = current().scene.get();
	scene->setDirection(tabState.direction);
	scene->togglePartitions(windowState->showAnnotations);
	scene->updateMarkers();
	// todo hack
	emit orderRequested(orderSelect->currentData().value<Dataset::OrderBy>(),
	                 !actionLockOrder->isChecked());
	view->setScene(scene);
}

void DistmatTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<DistmatScene>(data);

	auto scene = state.scene.get();

	/* connect outgoing signals */
	connect(scene, &DistmatScene::cursorChanged, this, &Viewer::cursorChanged);
}

/* Note: shared code between DistmatTab and HeatmapTab */
void DistmatTab::setupOrderUI()
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
	// TODO: do not sync order through gui! order is a part of dataset::structure state
	// or enforce it on dataset? if we make it per-dataset we can disable unavailable options…
	connect(this, &Viewer::changeOrder, [this] (auto order, bool sync) {
		const QSignalBlocker a(orderSelect), b(actionLockOrder);
		orderSelect->setCurrentText(Dataset::availableOrders().at(order));
		actionLockOrder->setChecked(!sync); // sync → lock
	});

	// remove container we picked from
	orderBar->deleteLater();
}

void DistmatTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
