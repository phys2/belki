#include "featweightstab.h"
#include "featweightsscene.h"

FeatweightsTab::FeatweightsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	setupWeightingUI();

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);
}

void FeatweightsTab::init(Dataset *data)
{
	scene = new FeatweightsScene(*data);

	connect(this, &Viewer::inToggleMarker, scene, &FeatweightsScene::toggleMarker);
	connect(this, &Viewer::inUpdateColorset, scene, &FeatweightsScene::updateColorset);
	connect(this, &Viewer::inReset, scene, &FeatweightsScene::reset);

	connect(scene, &FeatweightsScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	connect(actionToggleChart, &QAction::toggled, scene, &FeatweightsScene::toggleImage);

	auto syncWeighting = [this] {
		scene->changeWeighting(weightingSelect->currentData().value<FeatweightsScene::Weighting>());
	};
	connect(weightingSelect, QOverload<int>::of(&QComboBox::activated), syncWeighting);
	syncWeighting();

	view->setScene(scene);
}

void FeatweightsTab::setupWeightingUI()
{
	auto anchor = actionSavePlot;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, weightingLabel);
	toolBar->insertWidget(anchor, weightingSelect);

	for (auto &[v, n] : std::map<FeatweightsScene::Weighting, QString>{
	    {FeatweightsScene::Weighting::UNWEIGHTED, "Unweighted"},
	    {FeatweightsScene::Weighting::ABSOLUTE, "Absolute Target Distance"},
	    {FeatweightsScene::Weighting::RELATIVE, "Relative Target Distance"},
	    {FeatweightsScene::Weighting::OFFSET, "Offset Target Distance"},
    }) {
		weightingSelect->addItem(n, QVariant::fromValue(v));
	}
	weightingSelect->setCurrentIndex(1);

	weightingBar->deleteLater();
}
