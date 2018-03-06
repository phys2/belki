#include "profilewindow.h"
#include "mainwindow.h"

#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLineSeries>

#include <cmath>

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
	chart->setTitle(source->title());
	chart->legend()->setAlignment(Qt::AlignLeft);
	auto ax = new QtCharts::QCategoryAxis;
	auto ay = new QtCharts::QValueAxis;
	chart->setAxisX(ax);
	chart->setAxisY(ay);

	ax->setLabelsAngle(-90);
	ax->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
	auto labels = qobject_cast<QtCharts::QBarCategoryAxis*>(source->axisX())->categories();
	ax->setRange(0, labels.size() - 1);
	ay->setRange(0 , 1);

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

	auto sources = source->series();
	auto stats = computeMeanStddev(sources);

	// setup QAreaSeries for stddev
	if (!stats.first.empty()) {
		auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.first.size(); ++i) {
			upper->append(i, stats.first[i] + stats.second[i]);
			lower->append(i, stats.first[i] - stats.second[i]);
		}
		auto stddev = new QtCharts::QAreaSeries(upper, lower);
		addSeries(stddev, false);
		stddev->setName("σ (SD)");
		stddev->setColor(Qt::gray);
		stddev->setBorderColor(Qt::gray);
	}

	// sort series by name
	qSort(sources.begin(), sources.end(), [] (QtCharts::QAbstractSeries *a, QtCharts::QAbstractSeries *b) {
		return a->name() < b->name();
	});
	for (auto as : sources) {
		auto ls = qobject_cast<QtCharts::QLineSeries*>(as);
		auto t = new QtCharts::QLineSeries;
		addSeries(t, true);
		// copy by hand as copy constructor does not work (add to chart first)
		t->setName(ls->name());
		t->setBrush(ls->brush());
		t->setPen(ls->pen());
		t->replace(ls->pointsVector());
	}

	// setup QLineSeries for mean
	if (!stats.first.empty()) {
		auto average = new QtCharts::QLineSeries;
		addSeries(average, false);
		average->setName("Avg.");
		auto pen = average->pen();
		pen.setColor(Qt::black);
		pen.setWidthF(pen.widthF()*1.5);
		average->setPen(pen);
		for (unsigned i = 0; i < stats.first.size(); ++i) {
			average->append(i, stats.first[i]);
		}
	}

	/* actions */
	// standard keys not available in UI Designer
	actionSavePlot->setShortcut(QKeySequence::StandardKey::Print);

	connect(actionSavePlot, &QAction::triggered, [this] {
		auto parent = parentWidget();
		auto title = parent->getTitle();
		parent->getIo()->renderToFile(chartView, title, "Selected Profiles");
	});
	connect(actionShowLabels, &QAction::toggled, ax, toggleLabels);
	actionShowIndividual->setChecked(true);
	actionShowIndividual->setChecked(sources.size() < 50);
	if (!stats.first.empty()) {
		actionShowAverage->setChecked(true);
	} else {
		actionShowAverage->setDisabled(true);
	}

	/* we are a single popup thingy: self-show and self-delete on close */
	setAttribute(Qt::WidgetAttribute::WA_DeleteOnClose);
	setAttribute(Qt::WA_ShowWithoutActivating);
	show();
}

void ProfileWindow::addSeries(QtCharts::QAbstractSeries *s, bool individual)
{
	chart->addSeries(s);
	s->attachAxis(chart->axisX());
	s->attachAxis(chart->axisY());
	connect((individual ? actionShowIndividual : actionShowAverage), &QAction::toggled,
	        s, &QtCharts::QLineSeries::setVisible);
}

std::pair<std::vector<qreal>, std::vector<qreal>>
ProfileWindow::computeMeanStddev(const QList<QtCharts::QAbstractSeries *> &input)
{
	if (input.size() < 2)
		return {{}, {}};

	/* really not the brightest way to do this.
       maybe we should really just convert to sane formats and back */

	std::vector<qreal> mean;
	for (auto as : input) {
		auto points = qobject_cast<QtCharts::QLineSeries*>(as)->pointsVector();
		if (mean.empty()) {
			mean.resize((unsigned)points.size());
		}
		for (unsigned i = 0; i < mean.size(); ++i)
			mean[i] += points[(int)i].y();
	}
	for (auto &v : mean)
		v /= input.size();

	std::vector<qreal> stddev(mean.size());
	for (auto as : input) {
		auto points = qobject_cast<QtCharts::QLineSeries*>(as)->pointsVector();
		for (unsigned i = 0; i < mean.size(); ++i) {
			auto diff = points[(int)i].y() - mean[i];
			stddev[i] += diff*diff;
		}
	}
	for (auto &v : stddev) {
		v /= (input.size() - 1);
		v = std::sqrt(v);
	}

	return {mean, stddev};
}

MainWindow *ProfileWindow::parentWidget()
{
	return qobject_cast<MainWindow*>(QMainWindow::parentWidget());
}
