#include "profilewindow.h"
#include "profilechart.h"
#include "mainwindow.h"

ProfileWindow::ProfileWindow(ProfileChart *source, QWidget *parent) :
    QMainWindow(parent), chart(new ProfileChart(source)),
    mainWindow(qobject_cast<MainWindow*>(parent))
{
	if (!mainWindow)
		throw std::runtime_error("Parent of ProfileWindow is not a MainWindow!");

	setupUi(this);

	/* toolbar */
	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* chart */
	chartView->setChart(chart);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* actions */
	connect(actionSavePlot, &QAction::triggered, [this] {
		auto title = mainWindow->getTitle();
		auto desc = chart->title();
		if (desc.isEmpty())
			desc = "Selected Profiles";
		mainWindow->getIo()->renderToFile(chartView, {title, desc});
	});
	connect(actionShowLabels, &QAction::toggled, chart, &ProfileChart::toggleLabels);
	connect(actionShowIndividual, &QAction::toggled, chart, &ProfileChart::toggleIndividual);
	connect(actionShowAverage, &QAction::toggled, chart, &ProfileChart::toggleAverage);

	actionShowIndividual->setChecked(true); // cheap trick: let next one trigger
	actionShowIndividual->setChecked(chart->numProfiles() < 50);

	actionShowAverage->setEnabled(chart->haveStats());
	actionShowAverage->setChecked(chart->haveStats());

	/* we are a single popup thingy: self-show and self-delete on close */
	//setAttribute(Qt::WA_DeleteOnClose); CAUSES CRASH IN QT :-/ FIXME Who's gonna delete?
	//setAttribute(Qt::WA_ShowWithoutActivating); opens window in background (hidden)
	show();
}
