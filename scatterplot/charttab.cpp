#include "charttab.h"
#include "chart.h"
#include "dimred.h"

#include <QMenu>

ChartTab::ChartTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	view->setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5

	// setup toolbar
	auto anchor = actionComputeDisplay;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions (that don't depend on data/scene */
	connect(actionComputeDisplay, &QAction::triggered, [this] {
		auto methods = dimred::availableMethods();
		auto menu = new QMenu(this);
		for (const auto& m : methods) {
			if (transformSelect->findText(m.id) >= 0)
				continue;

			menu->addAction(m.description, [this, m] { emit computeDisplay(m.name); });
		}
		menu->popup(QCursor::pos());
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
// TODO		io->renderToFile(view, {title, transformSelect->currentText()});
	});
}

void ChartTab::init(Dataset *data)
{
	scene = new Chart(*data);
	view->setChart(scene);

	/* connect incoming/pass-through signals */
	connect(this, &Viewer::inUpdateColorset, scene, &Chart::updateColorset);
	connect(this, &Viewer::inReset, this, [this] (bool haveData) {
		// no displays
		transformSelect->clear();
		scene->clear();
		setEnabled(false);

		if (haveData)
			return; // not of our interest right now
	});
	connect(this, &Viewer::inRepartition, this, [this] {
		scene->clearPartitions();
		scene->updatePartitions();
	});
	// note: we ignore inReorder, as we don't use order
	connect(this, &Viewer::inToggleMarker, scene, &Chart::toggleMarker);

	/* connect outgoing signals */
	connect(scene, &Chart::markerToggled, this, &Viewer::markerToggled);
	connect(scene, &Chart::cursorChanged, this, &Viewer::cursorChanged);

	/* setup transform select in relationship with dataset */
	connect(transformSelect, &QComboBox::currentTextChanged, [this] (auto name) {
		if (name.isEmpty())
			return;
		scene->display(name);
		setEnabled(true);
	});
	connect(this, &ChartTab::computeDisplay, data, &Dataset::computeDisplay);
	connect(data, &Dataset::newDisplay, this, [this] (const QString &display) {
		transformSelect->addItem(display); // duplicates ignored
		transformSelect->setCurrentText(display);
	});
}
