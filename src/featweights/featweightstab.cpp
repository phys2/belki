#include "featweightstab.h"
#include "featweightsscene.h"
#include "../profiles/plotactions.h"

#include <QMainWindow>

FeatweightsTab::FeatweightsTab(QWidget *parent) :
    Viewer(new QMainWindow, parent)
{
	setupUi(qobject_cast<QMainWindow*>(widget));
	setupWeightingUI();

	// plug-in score stuff
	toolBar->addSeparator();
	scoreActions.push_back(toolBar->addWidget(scoreLabel));
	scoreActions.push_back(toolBar->addWidget(scoreSlider));

	auto capturePlot = PlotActions::createCapturePlotActions(toolBar);
	PlotActions::addCaptureButton(capturePlot, toolBar);

	stockpile->deleteLater();

	/* connect toolbar actions */
	connect(scoreSlider, &QSlider::valueChanged, [this] (int v) {
		double thresh = v * 0.01;
		if (!haveData() || selected().scoreThreshold == thresh)
			return;

		selected().scoreThreshold = thresh;
		bool isMaximum = v == scoreSlider->maximum(); // if so, disable cutoff
		selected().scene->applyScoreThreshold(isMaximum ? std::nan("") : thresh);
		updateScoreLabel();
	});
	connect(actionToggleChart, &QAction::toggled, [this] (bool useAlternate) {
		tabState.useAlternate = useAlternate;
		if (haveData()) selected().scene->toggleImage(useAlternate);
	});
	connect(weightingSelect, QOverload<int>::of(&QComboBox::activated), [this] () {
		tabState.weighting = weightingSelect->currentData().value<FeatweightsScene::Weighting>();
		if (haveData()) selected().scene->setWeighting(tabState.weighting);
	});
	auto requestExport = [this] (bool toFile) {
		emit exportRequested(view, "Feature Weighting", toFile);
	};
	for (auto act : {capturePlot.head, capturePlot.toFile})
		connect(act, &QAction::triggered, this, [requestExport] { requestExport(true); });
	connect(capturePlot.toClipboard, &QAction::triggered,
	        this, [requestExport] { requestExport(false); });

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

	updateScoreLabel();
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
	toolBar->addSeparator();
	toolBar->addWidget(weightingLabel);
	toolBar->addWidget(weightingSelect);

	for (auto &[v, n] : std::map<FeatweightsScene::Weighting, QString>{
	    {FeatweightsScene::Weighting::UNWEIGHTED, "Unweighted"},
	    {FeatweightsScene::Weighting::ABSOLUTE, "Absolute Target Distance"},
	    {FeatweightsScene::Weighting::RELATIVE, "Relative Target Distance"},
	    {FeatweightsScene::Weighting::OFFSET, "Offset Target Distance"},
    }) {
		weightingSelect->addItem(n, QVariant::fromValue(v));
	}
}

void FeatweightsTab::updateScoreLabel()
{
	if (!haveData())
		return;

	// note: ensure fixed text width to avoid the slider jumping around
	auto text = QString("Score thresh.: <b>%1</b> ")
	        .arg(QString::number(selected().scoreThreshold, 'f', 2));
	scoreLabel->setText(text);
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
	scoreSlider->blockSignals(true); // avoid mingling with datastate when prev. value was >limit
	scoreSlider->setMinimum(toInt(d->scoreRange.min));
	scoreSlider->setMaximum(toInt(d->scoreRange.max));
	scoreSlider->setTickInterval(scoreSlider->maximum() / 10); // TODO round numbers
	scoreSlider->setValue(toInt(selected().scoreThreshold));
	scoreSlider->blockSignals(false);
}

bool FeatweightsTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	widget->setEnabled(on);
	view->setVisible(on);
	return on;
}
