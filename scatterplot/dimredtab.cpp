#include "dimredtab.h"
#include "chart.h"
#include "dimred.h"

#include <QMenu>
#include <QToolButton>

DimredTab::DimredTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	view->setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	// let compute button display menu without holding mouse
	auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(actionComputeDisplay));
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions (that don't depend on data/scene) */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = transformSelect;
		s->setCurrentIndex((s->currentIndex() + 1) % s->count());
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = transformSelect;
		s->setCurrentIndex((s->count() + s->currentIndex() - 1) % s->count());
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, transformSelect->currentText());
	});
}

void DimredTab::init(Dataset *data)
{
	scene = new Chart(*data);
	scene->setTitles("dim 1", "dim 2");
	view->setChart(scene);

	/* connect incoming/pass-through signals */
	connect(this, &Viewer::inUpdateColorset, scene, &Chart::updateColorset);
	connect(this, &Viewer::inReset, this, [this] (bool) {
		// no displays
		transformSelect->clear();
		scene->clear();
		setEnabled(false);

		// note: as we are disabled the user has no chance to compute a display!
		// luckily, PCA is always computed by default
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

	/* setup transform select in relationship with dataset */
	connect(transformSelect, &QComboBox::currentTextChanged, [this, data] (auto name) {
		auto d = data->peek();
		if (name.isEmpty() || !d->display.contains(name))
			return;
		scene->display(d->display[name]);
		setEnabled(true);
	});
	connect(this, &DimredTab::computeDisplay, data, &Dataset::computeDisplay);
	connect(data, &Dataset::newDisplay, this, [this] (const QString &display) {
		transformSelect->addItem(display); // duplicates ignored
		transformSelect->setCurrentText(display);
		// remove from offered calculations
		updateComputeMenu();
	});
}

void DimredTab::updateComputeMenu() {
	auto methods = dimred::availableMethods();
	auto menu = new QMenu(this->window());
	for (const auto& m : methods) {
		if (transformSelect->findText(m.id) >= 0)
			continue;

		menu->addAction(m.description, [this, m] { emit computeDisplay(m.name); });
	}
	actionComputeDisplay->setMenu(menu);
}
