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
		if (!haveData())
			return;

		selected().scoreThreshold = v;
		bool isMaximum = v == scoreSlider->maximum(); // if so, disable cutoff
		selected().scene->applyScoreThreshold(toDouble(isMaximum ? std::nan("") : v));
	});
	connect(actionToggleChart, &QAction::toggled, [this] (bool useAlternate) {
		tabState.useAlternate = useAlternate;
		if (haveData()) selected().scene->toggleImage(useAlternate);
	});
	connect(weightingSelect, QOverload<int>::of(&QComboBox::activated), [this] () {
		tabState.weighting = weightingSelect->currentData().value<FeatweightsScene::Weighting>();
		if (haveData()) selected().scene->setWeighting(tabState.weighting);
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Distance Matrix");
	});

	/* propagate initial state */
	actionToggleChart->setChecked(tabState.useAlternate);
	weightingSelect->setCurrentIndex(weightingSelect->findData(
	                                     QVariant::fromValue(FeatweightsScene::Weighting::OFFSET)));

	updateIsEnabled();
}

void FeatweightsTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);

	/* connect state change signals */
	auto ws = s.get();
	connect(ws, &WindowState::colorsetUpdated, this, [this] () {
		if (haveData())
			selected().scene->updateColorset(windowState->standardColors);
	});
	connect(&s->proteins(), &ProteinDB::markersToggled, this, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (haveData())
			selected().scene->toggleMarkers(ids, present);
	});
}

void FeatweightsTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	updateScoreSlider();

	// pass guiState onto scene
	auto scene = selected().scene.get();
	scene->updateColorset(windowState->standardColors);
	scene->setWeighting(weightingSelect->currentData().value<FeatweightsScene::Weighting>());
	scene->toggleImage(tabState.useAlternate);
	scene->updateMarkers();
	view->setScene(scene);
}

void FeatweightsTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.scoreThreshold = (data->peek<Dataset::Base>()->hasScores() ?
	                            data->peek<Dataset::Base>()->scoreRange.max : 0);
	state.scene = std::make_unique<FeatweightsScene>(data);

	auto scene = state.scene.get();

	/* connect outgoing signals */
	connect(scene, &FeatweightsScene::cursorChanged, this, &Viewer::proteinsHighlighted);
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
	if (!haveData())
		return;
	auto d = selected().data->peek<Dataset::Base>();

	for (auto i : scoreActions)
		i->setVisible(d->hasScores());
	if (!d->hasScores())
		return;

	auto toInt = [] (auto value) { return (int)(value * 100); };
	scoreSlider->setMinimum(toInt(d->scoreRange.min));
	scoreSlider->setMaximum(toInt(d->scoreRange.max));
	scoreSlider->setTickInterval(scoreSlider->maximum() / 10); // TODO round numbers
	scoreSlider->setValue(toInt(selected().scoreThreshold));
}

bool FeatweightsTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	setEnabled(on);
	view->setVisible(on);
	return on;
}
