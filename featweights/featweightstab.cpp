#include "featweightstab.h"
#include "featweightsscene.h"

FeatweightsTab::FeatweightsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	setupWeightingUI();

	auto anchor = actionSavePlot;

	// plug-in score stuff
	toolBar->insertSeparator(anchor);
	scoreActions.push_back(toolBar->insertWidget(anchor, scoreLabel));
	scoreActions.push_back(toolBar->insertWidget(anchor, scoreSlider));
	connect(scoreSlider, &QSlider::valueChanged, [this] (int v) {
		// note: ensure fixed text width to avoid the slider jumping around
		auto text = QString("Score thresh.: <b>%1</b> ")
		        .arg(QString::number(v * 0.01, 'f', 2));
		scoreLabel->setText(text);
	});

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	stockpile->deleteLater();
}

void FeatweightsTab::init(Dataset *data)
{
	scene = std::make_unique<FeatweightsScene>(*data);

	connect(this, &Viewer::inToggleMarker, scene.get(), &FeatweightsScene::toggleMarker);
	connect(this, &Viewer::inUpdateColorset, scene.get(), &FeatweightsScene::updateColorset);
	connect(this, &Viewer::inReset, scene.get(), &FeatweightsScene::reset);

	connect(scene.get(), &FeatweightsScene::cursorChanged, this, &Viewer::cursorChanged);

	connect(this, &Viewer::inReset, [this, data] (bool haveData) {
		// we are good to go on reset(true), but not on reset(false)
		setEnabled(haveData);

		// adapt score state
		auto d = data->peek();
		for (auto i : scoreActions)
			i->setVisible(haveData && d->hasScores());
		if (d->hasScores()) {
			scoreSlider->setMinimum((int)(d->scoreRange.min * 100));
			scoreSlider->setMaximum((int)(d->scoreRange.max * 100));
			scoreSlider->setTickInterval(scoreSlider->maximum() / 10); // TODO round numbers
			scoreSlider->setValue(scoreSlider->maximum());
		}
	});

	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	connect(actionToggleChart, &QAction::toggled, scene.get(), &FeatweightsScene::toggleImage);

	auto syncWeighting = [this] {
		scene->changeWeighting(weightingSelect->currentData().value<FeatweightsScene::Weighting>());
	};
	connect(weightingSelect, QOverload<int>::of(&QComboBox::activated), syncWeighting);
	syncWeighting();

	connect(scoreSlider, &QSlider::valueChanged, [this] (int v) {
		scene->applyScoreThreshold(v * 0.01);
	});

	view->setScene(scene.get());
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
}
