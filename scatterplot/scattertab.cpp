#include "scattertab.h"
#include "chart.h"

#include <QMenu>
#include <QDebug>

ScatterTab::ScatterTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	view->setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, dimensionLabel);
	toolBar->insertWidget(anchor, dimensionSelect);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = dimensionSelect;
		selectDimension((s->currentIndex() + 1) % s->count());
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = dimensionSelect;
		selectDimension((s->count() + s->currentIndex() - 1) % s->count());
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, dimensionSelect->currentText());
	});
	connect(dimensionSelect, qOverload<int>(&QComboBox::activated),
	        this, &ScatterTab::selectDimension);

	/* connect incoming signals */
	connect(this, &Viewer::inTogglePartitions, [this] (bool show) {
		guiState.showPartitions = show;
		if (current)
			current().scene->togglePartitions(show);
	});
	connect(this, &Viewer::inToggleMarkers, [this] (auto ids, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarkers(ids, present);
	});

	updateEnabled();
}

ScatterTab::~ScatterTab()
{
	view->setChart(new QtCharts::QChart()); // release ownership
}

void ScatterTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	dimensionSelect->clear();
	bool enabled = updateEnabled();
	if (!enabled)
		return;

	// refill dimensionSelect
	dimensionSelect->addItems(current().data->peek<Dataset::Base>()->dimensions);
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(dimensionSelect->count() > 1);
	if (current().dimension < 0) // scene is still empty
		selectDimension(0);
	else
		dimensionSelect->setCurrentIndex(current().dimension);

	// pass guiState onto chart
	auto scene = current().scene.get();
	scene->togglePartitions(guiState.showPartitions);
	scene->updateMarkers();
	view->setChart(scene);
}

void ScatterTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.hasScores = data->peek<Dataset::Base>()->hasScores();
	state.scene = std::make_unique<Chart>(data);

	auto scene = state.scene.get();
	scene->setTitles("Value", "Score");

	/* connect outgoing signals */
	connect(scene, &Chart::markerToggled, this, &Viewer::markerToggled);
	connect(scene, &Chart::cursorChanged, this, &Viewer::cursorChanged);
}

void ScatterTab::selectDimension(int index)
{
	current().dimension = index;
	dimensionSelect->setCurrentIndex(index);
	auto d = current().data->peek<Dataset::Base>();
	QVector<QPointF> points(d->features.size());
	for (size_t i = 0; i < d->features.size(); ++i)
		points[i] = {d->features[i][(size_t)index], d->scores[i][(size_t)index]};
	d.unlock();
	current().scene->display(points);
}

bool ScatterTab::updateEnabled()
{
	// we can only display if have scores on y-axis
	bool on = current && current().hasScores;
	setEnabled(on);
	view->setVisible(on);

	return on;
}
