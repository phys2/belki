#include "featweightstab.h"
#include "featweightsscene.h"

FeatweightsTab::FeatweightsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);
}

void FeatweightsTab::init(Dataset *data)
{
	scene = new FeatweightsScene(*data);

	connect(this, &Viewer::inUpdateColorset, scene, &FeatweightsScene::updateColorset);
	connect(this, &Viewer::inReset, scene, &FeatweightsScene::reset);

	connect(scene, &FeatweightsScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	view->setScene(scene);
}
