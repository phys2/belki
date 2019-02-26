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
}

void HeatmapTab::init(Dataset *data)
{
	scene = new HeatmapScene(*data);

	connect(this, &Viewer::inUpdateColorset, scene, &HeatmapScene::updateColorset);
	connect(this, &Viewer::inReset, scene, &HeatmapScene::reset);
	connect(this, &Viewer::inRepartition, [this] (bool withOrder) {
		if (withOrder)
			scene->reorder();
		scene->recolor();
	});
	connect(this, &Viewer::inReorder, scene, &HeatmapScene::reorder);
	connect(this, &Viewer::inToggleMarker, scene, &HeatmapScene::toggleMarker);

	connect(scene, &HeatmapScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	connect(actionToggleSingleCol, &QAction::toggled, view, &HeatmapView::setColumnMode);
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(scene, "Heatmap");
	});

	view->setScene(scene);
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
