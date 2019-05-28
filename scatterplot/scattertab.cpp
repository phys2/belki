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
		s->setCurrentIndex((s->currentIndex() + 1) % s->count());
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = dimensionSelect;
		s->setCurrentIndex((s->count() + s->currentIndex() - 1) % s->count());
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, dimensionSelect->currentText());
	});
	connect(dimensionSelect, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        [this] (int index) {
		if (index < 0 || !current)
			return;
		current().dimension = index;
		auto d = current().data->peek<Dataset::Base>();
		QVector<QPointF> points(d->features.size());
		for (size_t i = 0; i < d->features.size(); ++i)
			points[i] = {d->features[i][(size_t)index], d->scores[i][(size_t)index]};
		d.unlock();
		current().scene->display(points);
	});

	/* connect incoming signals */
	connect(this, &Viewer::inUpdateColorset, [this] (auto colors) {
		guiState.colorset = colors;
		if (current)
			current().scene->updateColorset(colors);
	});
	connect(this, &Viewer::inTogglePartitions, [this] (bool show) {
		guiState.showPartitions = show;
		if (current)
			current().scene->togglePartitions(show);
	});
	connect(this, &Viewer::inToggleMarker, [this] (ProteinId id, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarker(id, present);
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

	// pass guiState onto chart
	auto scene = current().scene.get();
	scene->updateColorset(guiState.colorset);
	scene->togglePartitions(guiState.showPartitions);
	// TODO: tell chart to update markers
	view->setChart(scene);

	// fill up dimensionSelect
	dimensionSelect->addItems(current().data->peek<Dataset::Base>()->dimensions);
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(dimensionSelect->count() > 1);
	dimensionSelect->setCurrentIndex((int)current().dimension); // triggers
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

bool ScatterTab::updateEnabled()
{
	// we can only display if have scores on y-axis
	bool on = current && current().hasScores;
	setEnabled(on);
	view->setVisible(on);

	return on;
}
