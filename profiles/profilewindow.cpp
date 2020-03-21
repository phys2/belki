#include "profilewindow.h"
#include "profilechart.h"
#include "widgets/mainwindow.h"
#include "windowstate.h"
#include "fileio.h"

ProfileWindow::ProfileWindow(std::shared_ptr<WindowState> state, ProfileChart *source, QWidget *parent) :
    QMainWindow(parent), chart(new ProfileChart(source))
{
	setupUi(this);

	/* toolbar */
	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* chart */
	/* note: we do not use OpenGL as it has drawing bugs / does not support our
	 * customizations for score points */
	chartView->setChart(chart);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* actions */
	connect(actionSavePlot, &QAction::triggered, [this,state] {
		auto title = chart->dataset()->config().name;
		auto desc = chart->title();
		if (desc.isEmpty())
			desc = "Selected Profiles";
		state->io().renderToFile(chartView, {title, desc});
	});
	connect(actionShowLabels, &QAction::toggled, chart, &ProfileChart::toggleLabels);
	connect(actionShowIndividual, &QAction::toggled, chart, &ProfileChart::toggleIndividual);
	connect(actionShowAverage, &QAction::toggled, chart, &ProfileChart::toggleAverage);
	connect(actionLogarithmic, &QAction::toggled, chart, &ProfileChart::toggleLogSpace);

	actionShowAverage->setEnabled(chart->numProfiles() >= 2);
	actionShowAverage->setChecked(chart->numProfiles() >= 10);
	actionShowIndividual->setChecked(chart->numProfiles() < 50);

	chart->toggleAverage(actionShowAverage->isChecked());
	chart->toggleIndividual(actionShowIndividual->isChecked());

	actionLogarithmic->setChecked(chart->isLogSpace());

	chart->finalize();

	/* we are a single popup thingy: self-show and self-delete on close */
	//setAttribute(Qt::WA_DeleteOnClose); CAUSES CRASH IN QT :-/ FIXME Who's gonna delete? (eventually parent will)
	//setAttribute(Qt::WA_ShowWithoutActivating); opens window in background (hidden)
	show();
}
