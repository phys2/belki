#include "heatmaptab.h"
#include "heatmapscene.h"
#include "jobregistry.h"
#include "../profiles/plotactions.h"

#include <QMainWindow>

HeatmapTab::HeatmapTab(QWidget *parent) :
    Viewer(new QMainWindow, parent)
{
	setupUi(qobject_cast<QMainWindow*>(widget));
	setupOrderUI();

	auto capturePlot = PlotActions::createCapturePlotActions(toolBar);
	PlotActions::addCaptureButton(capturePlot, toolBar);

	/* connect toolbar actions */
	connect(actionToggleSingleCol, &QAction::toggled, [this] (bool toggle) {
		tabState.singleColumn = toggle;
		if (haveData())
			view->setColumnMode(toggle);
	});
	auto requestExport = [this] (bool toFile) {
		if (haveData())
			emit exportRequested(selected().scene.get(), "Heatmap", toFile);
	};
	for (auto act : {capturePlot.head, capturePlot.toFile})
		connect(act, &QAction::triggered, this, [requestExport] { requestExport(true); });
	connect(capturePlot.toClipboard, &QAction::triggered,
	        this, [requestExport] { requestExport(false); });

	/* propagate initial state */
	actionToggleSingleCol->setChecked(tabState.singleColumn);

	updateIsEnabled();
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
	connect(&s->proteins(), &ProteinDB::markersToggled, this, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (haveData())
			selected().scene->toggleMarkers(ids, present);
	});
}

void HeatmapTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	view->switchScene(selected().scene.get());
	view->setColumnMode(tabState.singleColumn);
}

void HeatmapTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.scene = std::make_unique<HeatmapScene>(data);

	auto scene = state.scene.get();
	scene->setState(windowState);

	/* connect outgoing signals */
	connect(scene, &HeatmapScene::cursorChanged, this, &Viewer::proteinsHighlighted);
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
		if (haveData()) {
			Task task{[s=windowState,d=selected().data] { d->computeOrder(s->order); },
				      Task::Type::ORDER,
				      {orderSelect->currentText(), selected().data->config().name}};
			JobRegistry::run(task, windowState->jobMonitors);
		}
	});
	connect(actionLockOrder, &QAction::toggled, [this] {
		windowState->orderSynchronizing = !actionLockOrder->isChecked();
		emit windowState->orderSynchronizingToggled();
	});

	// remove container we picked from
	orderBar->deleteLater();
}

bool HeatmapTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	widget->setEnabled(on);
	view->setVisible(on);
	return on;
}
