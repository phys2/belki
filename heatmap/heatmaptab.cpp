#include "heatmaptab.h"
#include "heatmapscene.h"

HeatmapTab::HeatmapTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);

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
	connect(this, &Viewer::inRepartition, scene, &HeatmapScene::recolor);
	connect(this, &Viewer::inReorder, scene, &HeatmapScene::reorder);
	connect(this, &Viewer::inToggleMarker, scene, &HeatmapScene::toggleMarker);

	connect(scene, &HeatmapScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	connect(actionToggleSingleCol, &QAction::toggled, view, &HeatmapView::setColumnMode);

	view->setScene(scene);
}
