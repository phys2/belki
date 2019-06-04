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

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	stockpile->deleteLater();

	/* connect toolbar actions */
	connect(scoreSlider, &QSlider::valueChanged, [this] (int v) {
		auto toDouble = [] (auto value) { return value * 0.01; };
		// note: ensure fixed text width to avoid the slider jumping around
		auto text = QString("Score thresh.: <b>%1</b> ")
		        .arg(QString::number(toDouble(v), 'f', 2));
		scoreLabel->setText(text);
		if (!current)
			return;

		current().scoreThreshold = v;
		bool isMaximum = v == scoreSlider->maximum(); // if so, disable cutoff
		current().scene->applyScoreThreshold(toDouble(isMaximum ? std::nan("") : v));
	});
	connect(actionToggleChart, &QAction::toggled, [this] (bool useAlternate) {
		guiState.useAlternate = useAlternate;
		if (current)
			current().scene->toggleImage(useAlternate);
	});
	connect(weightingSelect, QOverload<int>::of(&QComboBox::activated), [this] () {
		guiState.weighting = weightingSelect->currentData().value<FeatweightsScene::Weighting>();
		if (current)
			current().scene->setWeighting(guiState.weighting);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	/* connect incoming signals */
	connect(this, &Viewer::inUpdateColorset, [this] (auto colors) {
		guiState.colorset = colors;
		if (current)
			current().scene->updateColorset(colors);
	});
	connect(this, &Viewer::inToggleMarker, [this] (ProteinId id, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarker(id, present);
	});

	/* propagate initial state */
	actionToggleChart->setChecked(guiState.useAlternate);
	weightingSelect->setCurrentIndex(weightingSelect->findData(
	                                     QVariant::fromValue(FeatweightsScene::Weighting::OFFSET)));

	updateEnabled();
}

void FeatweightsTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	updateScoreSlider();

	// pass guiState onto scene
	auto scene = current().scene.get();
	scene->updateColorset(guiState.colorset);
	scene->setWeighting(weightingSelect->currentData().value<FeatweightsScene::Weighting>());
	scene->toggleImage(guiState.useAlternate);
	scene->updateMarkers();
	view->setScene(scene);
}

void FeatweightsTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scoreThreshold = (data->peek<Dataset::Base>()->hasScores() ?
	                            data->peek<Dataset::Base>()->scoreRange.max : 0);
	state.scene = std::make_unique<FeatweightsScene>(data);

	auto scene = state.scene.get();

	/* connect outgoing signals */
	connect(scene, &FeatweightsScene::cursorChanged, this, &Viewer::cursorChanged);
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
}

void FeatweightsTab::updateScoreSlider()
{
	auto d = current().data->peek<Dataset::Base>();

	for (auto i : scoreActions)
		i->setVisible(d->hasScores());
	if (!d->hasScores())
		return;

	auto toInt = [] (auto value) { return (int)(value * 100); };
	scoreSlider->setMinimum(toInt(d->scoreRange.min));
	scoreSlider->setMaximum(toInt(d->scoreRange.max));
	scoreSlider->setTickInterval(scoreSlider->maximum() / 10); // TODO round numbers
	scoreSlider->setValue(toInt(current().scoreThreshold));
}

void FeatweightsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
