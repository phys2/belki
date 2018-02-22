#include "profilewindow.h"
#include "mainwindow.h"

#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLineSeries>

#include <QtDebug>

ProfileWindow::ProfileWindow(QtCharts::QChart *source, MainWindow *parent) :
    QMainWindow(parent), chart(new QtCharts::QChart)
{
	setupUi(this);

	/* toolbar */
	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* chart */
	chartView->setChart(chart);
	chartView->setRenderHint(QPainter::Antialiasing);
	chart->legend()->setAlignment(Qt::AlignLeft);
	auto ax = new QtCharts::QCategoryAxis;
	auto ay = new QtCharts::QValueAxis;
	chart->setAxisX(ax);
	chart->setAxisY(ay);

	ax->setLabelsAngle(-90);
	ax->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
	auto labels = qobject_cast<QtCharts::QBarCategoryAxis*>(source->axisX())->categories();
	ax->setRange(0, labels.size());
	auto toggleLabels = [ax, labels] (bool on) {
		/* QCategoryAxis does not adapt geometry when simply hiding labels. And
		 * it makes it really complicated for us to replace them :/ */
		if (on) {
			auto i = 0;
			for (auto &l : qAsConst(labels)) {
				ax->append(l, i++);
			}
		} else {
			auto categories = ax->categoriesLabels();
			for (auto &l : qAsConst(categories))
				ax->remove(l);
		}
	};
	toggleLabels(false);

	// sort by name
	QList<QtCharts::QAbstractSeries*> series = source->series();
	qSort(series.begin(), series.end(), [] (QtCharts::QAbstractSeries *a, QtCharts::QAbstractSeries *b) {
		return a->name() < b->name();
	});

	// TODO setup QAreaSeries for stddev here

	for (auto as : series) {
		auto ls = qobject_cast<QtCharts::QLineSeries*>(as);
		auto t = new QtCharts::QLineSeries;
		chart->addSeries(t);
		t->attachAxis(chart->axisX());
		t->attachAxis(chart->axisY());
		t->setName(ls->name());
		t->setBrush(ls->brush());
		t->setPen(ls->pen());
		t->replace(ls->pointsVector());
	}

	// TODO setup QLineSeries for avg. here

	/* actions */
	// standard keys not available in UI Designer
	actionSavePlot->setShortcut(QKeySequence::StandardKey::Print);

	connect(actionSavePlot, &QAction::triggered, [this] {
		auto parent = parentWidget();
		auto title = parent->getTitle();
		parent->getIo()->renderToFile(chartView, title, "Selected Profiles");
	});
	connect(actionShowLabels, &QAction::toggled, ax, toggleLabels);

	/* we are a single popup thingy: self-show and self-delete on close */
	setAttribute(Qt::WidgetAttribute::WA_DeleteOnClose);
	setAttribute(Qt::WA_ShowWithoutActivating);
	show();
}

MainWindow *ProfileWindow::parentWidget()
{
	return qobject_cast<MainWindow*>(QMainWindow::parentWidget());
}
