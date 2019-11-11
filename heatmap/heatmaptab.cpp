#include "heatmaptab.h"
#include "heatmapscene.h"

#include <QtConcurrent>

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

	/* propagate initial state */
	actionToggleSingleCol->setChecked(tabState.singleColumn);

	updateEnabled();
}

void HeatmapTab::setWindowState(std::shared_ptr<WindowState> s)
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
	connect(&s->proteins(), &ProteinDB::markersToggled, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarkers(ids, present);
	});
}

void HeatmapTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	view->switchScene(current().scene.get());
	view->setColumnMode(tabState.singleColumn);
}

void HeatmapTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<HeatmapScene>(data);

	auto scene = state.scene.get();
	scene->setState(windowState);

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

	// signalling
	connect(orderSelect, QOverload<int>::of(&QComboBox::activated), [this] {
		windowState->setOrder(orderSelect->currentData().value<Order::Type>());
		if (current)
			QtConcurrent::run([d=current().data,o=windowState->order] {
				d->prepareOrder(o);
			});
	});
	connect(actionLockOrder, &QAction::toggled, [this] {
		windowState->orderSynchronizing = !actionLockOrder->isChecked();
		emit windowState->orderSynchronizingToggled();
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
