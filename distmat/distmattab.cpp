#include "distmattab.h"
#include "distmatscene.h"
#include "jobregistry.h"

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
		if (haveData())
			selected().scene->setDirection(tabState.direction);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	/* propagate initial state */
	actionToggleDistdir->setChecked(tabState.direction == Dataset::Direction::PER_DIMENSION);

	updateIsEnabled();
}

void DistmatTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	orderSelect->setModel(&s->orderModel);
	orderSelect->setCurrentIndex(orderSelect->findData(s->preferredOrder));
	actionLockOrder->setChecked(!s->orderSynchronizing);

	/* connect state change signals (specify receiver so signal is cleaned up!) */
	auto ws = s.get();
	connect(ws, &WindowState::orderChanged, this, [this] {
		orderSelect->setCurrentIndex(orderSelect->findData(windowState->preferredOrder));
	});
	connect(ws, &WindowState::orderSynchronizingToggled, this, [this] {
		actionLockOrder->setChecked(!windowState->orderSynchronizing);
	});
	connect(&s->proteins(), &ProteinDB::markersToggled, this, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (haveData())
			selected().scene->toggleMarkers(ids, present);
	});
}

void DistmatTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	auto scene = selected().scene.get();
	scene->setDirection(tabState.direction);
	view->switchScene(scene);
}

void DistmatTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.scene = std::make_unique<DistmatScene>(data);

	auto scene = state.scene.get();
	scene->setState(windowState);

	/* connect outgoing signals */
	connect(scene, &DistmatScene::cursorChanged, this, &Viewer::proteinsHighlighted);
}

/* Note: shared code between DistmatTab and HeatmapTab */
void DistmatTab::setupOrderUI()
{
	// setup toolbar
	auto anchor = actionLockOrder;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, orderLabel);
	toolBar->insertWidget(anchor, orderSelect);

	// signalling
	connect(orderSelect, QOverload<int>::of(&QComboBox::activated), [this] {
		windowState->setOrder(orderSelect->currentData().value<Order::Type>());
		if (haveData()) {
			Task task{[s=windowState,d=selected().data] { d->prepareOrder(s->order); },
				      Task::Type::ORDER,
				      {orderSelect->currentText(), selected().data->config().name}};
			JobRegistry::run(task, windowState->jobListeners);
		}
	});
	connect(actionLockOrder, &QAction::toggled, [this] {
		windowState->orderSynchronizing = !actionLockOrder->isChecked();
		emit windowState->orderSynchronizingToggled();
	});

	// remove container we picked from
	orderBar->deleteLater();
}

bool DistmatTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	setEnabled(on);
	view->setVisible(on);
	return on;
}
