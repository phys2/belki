#include "scattertab.h"
#include "chart.h"
#include "dimred.h"

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

	/* connect toolbar actions (that don't depend on data/scene) */
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
}

void ScatterTab::init(Dataset *data)
{
	scene = new Chart(*data);
	scene->setTitles("Value", "Score");
	view->setChart(scene);

	/* connect incoming/pass-through signals */
	connect(this, &Viewer::inUpdateColorset, scene, &Chart::updateColorset);
	connect(this, &Viewer::inReset, this, [this, data] (bool haveData) {
		// we can only display if have scores on y-axis
		setEnabled(haveData && data->peek()->hasScores());

		if (!isEnabled()) {
			dimensionSelect->clear();
			scene->clear();
			return;
		}

		// fill up dimensionSelect
		dimensionSelect->addItems(data->peek()->dimensions);
		dimensionSelect->setCurrentIndex(0);
	});
	connect(this, &Viewer::inRepartition, this, [this] {
		scene->clearPartitions();
		scene->updatePartitions();
	});
	// note: we ignore inReorder, as we don't use protein order
	connect(this, &Viewer::inToggleMarker, scene, &Chart::toggleMarker);
	connect(this, &Viewer::inTogglePartitions, scene, &Chart::togglePartitions);

	/* connect outgoing signals */
	connect(scene, &Chart::markerToggled, this, &Viewer::markerToggled);
	connect(scene, &Chart::cursorChanged, this, &Viewer::cursorChanged);

	/* setup dimension select in relationship with dataset */
	connect(dimensionSelect, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        [this, data] (int index) {
		if (index < 0)
			return;
		auto d = data->peek();
		QVector<QPointF> points(d->features.size());
		for (size_t i = 0; i < d->features.size(); ++i)
			points[i] = {d->features[i][(size_t)index], d->scores[i][(size_t)index]};
		scene->display(points);
	});
}
