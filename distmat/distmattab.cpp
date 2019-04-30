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
}

void DistmatTab::init(Dataset *data)
{
	scene = std::make_unique<DistmatScene>(*data);

	connect(this, &Viewer::inUpdateColorset, scene.get(), &DistmatScene::updateColorset);
	connect(this, &Viewer::inReset, scene.get(), &DistmatScene::reset);
	connect(this, &Viewer::inRepartition, [this] (bool withOrder) {
		if (withOrder)
			scene->reorder(); // implies recolor()
		else
			scene->recolor();
	});
	connect(this, &Viewer::inReorder, scene.get(), &DistmatScene::reorder);
	connect(this, &Viewer::inToggleMarker, scene.get(), &DistmatScene::toggleMarker);
	connect(this, &Viewer::inTogglePartitions, scene.get(), &DistmatScene::togglePartitions);

	connect(scene.get(), &DistmatScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	auto toggleDirection = [this] (bool toggle) {
		scene->setDirection(toggle ? DistmatScene::Direction::PER_DIMENSION
		                           : DistmatScene::Direction::PER_PROTEIN);
	};
	connect(actionToggleDistdir, &QAction::toggled, toggleDirection);
	toggleDirection(actionToggleDistdir->isChecked());

	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	view->setScene(scene.get());
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
	connect(this, &Viewer::changeOrder, [this] (auto order, bool sync) {
		const QSignalBlocker a(orderSelect), b(actionLockOrder);
		orderSelect->setCurrentText(Dataset::availableOrders().at(order));
		actionLockOrder->setChecked(!sync); // sync → lock
	});

	// remove container we picked from
	orderBar->deleteLater();
}
