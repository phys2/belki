#include "scattertab.h"
#include "chart.h"
#include "compute/features.h"

ScatterTab::ScatterTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, dimensionLabel);
	toolBar->insertWidget(anchor, dimXSelect);

	anchor = actionSavePlot;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, dimensionLabel_2);
	toolBar->insertWidget(anchor, dimYSelect);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(anchor, spacer);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = dimXSelect;
		selectDimension((s->currentIndex() + 1) % s->count());
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = dimXSelect;
		selectDimension((s->count() + s->currentIndex() - 1) % s->count());
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		auto descript = QString("%1 – %2")
		        .arg(dimXSelect->currentText(), dimYSelect->currentText());
		emit exportRequested(view, descript);
	});
	connect(dimXSelect, qOverload<int>(&QComboBox::activated),
	        this, &ScatterTab::selectDimension);
	connect(dimYSelect, qOverload<int>(&QComboBox::activated),
	        this, &ScatterTab::selectSecondaryDimension);

	/* connect incoming signals */
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarkers(ids, present);
	});

	updateEnabled();
}

ScatterTab::~ScatterTab()
{
	view->releaseChart(); // avoid double delete
}

void ScatterTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	view->toggleOpenGL(s->useOpenGl);

	/* connect state change signals */
	auto ws = s.get();
	connect(ws, &WindowState::annotationsToggled, [this] () {
		if (current)
			current().scene->togglePartitions(windowState->showAnnotations);
	});
	connect(ws, &WindowState::openGlToggled, [this] () {
		view->toggleOpenGL(windowState->useOpenGl);
	});
}

void ScatterTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	bool enabled = updateEnabled();
	if (!enabled)
		return;

	// update dimensionSelects
	refillDimensionSelects();
	if (current().dimension < 0) {
		selectDimension(0);  // scene is still empty
	} else {
		// dimX has a direct index mapping, but dimY uses userData (due to index shifts)
		dimXSelect->setCurrentIndex(current().dimension);
		dimYSelect->setCurrentIndex(dimYSelect->findData(current().secondaryDimension));
	}

	// pass guiState onto chart
	auto scene = current().scene.get();
	scene->togglePartitions(windowState->showAnnotations);
	scene->updateMarkers();
	view->switchChart(scene);
}

void ScatterTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.hasScores = data->peek<Dataset::Base>()->hasScores();
	if (!state.hasScores)
		state.secondaryDimension = 1;
	state.scene = std::make_unique<Chart>(data, view->getConfig());

	auto scene = state.scene.get();

	/* connect outgoing signals */
	connect(scene, &Chart::markerToggled, this, &Viewer::markerToggled);
	connect(scene, &Chart::cursorChanged, this, &Viewer::cursorChanged);
}

void ScatterTab::refillDimensionSelects(bool onlySecondary)
{
	auto d = current().data->peek<Dataset::Base>();

	// re-fill dimYSelect
	dimYSelect->clear();
	if (current().hasScores)
		dimYSelect->addItem("Score", -1);
	for (auto i = 0; i < d->dimensions.size(); ++i) {
		//if (i == current().dimension)	continue;
		dimYSelect->addItem(d->dimensions.at(i), i);
	}

	if (onlySecondary)
		return;

	// re-fill dimXSelect
	dimXSelect->clear();
	dimXSelect->addItems(d->dimensions);
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(dimXSelect->count() > 1);
}

void ScatterTab::selectDimension(int index)
{
	current().dimension = index;
	dimXSelect->setCurrentIndex(index);

	// update dimY state and plot
	refillDimensionSelects(true);
	selectSecondaryDimension(dimYSelect->findData(current().secondaryDimension));
}

void ScatterTab::selectSecondaryDimension(int index)
{
	current().secondaryDimension = dimYSelect->itemData(index).toInt();
	dimYSelect->setCurrentIndex(index);

	bool displayScores = current().secondaryDimension < 0;

	auto d = current().data->peek<Dataset::Base>();
	auto dim = (size_t)current().dimension;
	auto dim2 = (size_t)current().secondaryDimension;
	auto points = (displayScores
	               ? features::scatter(d->features, dim, d->scores, dim)
	               : features::scatter(d->features, dim, d->features, dim2));
	d.unlock();
	current().scene->display(points);

	if (displayScores) {
		current().scene->setTitles("Value", "Score");
	} else {
		current().scene->setTitles(dimXSelect->currentText(), dimYSelect->currentText());
	}
}

bool ScatterTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);

	return on;
}
