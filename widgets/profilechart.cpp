#include "profilechart.h"
#include "profilewindow.h"

#include <QLineSeries>
#include <QAreaSeries>
#include <QValueAxis>
#include <QCategoryAxis>
#include <QBarCategoryAxis>

#include <cmath>

#include <QtDebug>

ProfileChart::ProfileChart()
{
	/* small plot constructor */

	ax = new QtCharts::QBarCategoryAxis;
	ay = new QtCharts::QValueAxis;
	ay->setRange(0, 1);
	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);
	for (auto a : {ax, ay})
		a->hide();
	legend()->hide();
}

ProfileChart::ProfileChart(ProfileChart *source)
{
	/* big, labelled plot constructor */
	auto ax = new QtCharts::QCategoryAxis;
	this->ax = ax; // keep QCategoryAxis* in this method
	ay = new QtCharts::QValueAxis;
	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);

	setTitle(source->title());
	legend()->setAlignment(Qt::AlignLeft);

	ax->setLabelsAngle(-90);
	ax->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
	auto labels = qobject_cast<QtCharts::QBarCategoryAxis*>(source->ax)->categories();
	ax->setRange(0, labels.size() - 1);
	ay->setRange(0, 1);

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
	connect(this, &ProfileChart::toggleLabels, toggleLabels);

	/* copy over content
	 * Series are non-copyable, so we just recreate them */
	stats = source->stats;
	for (auto i : source->content)
		addSample(i->name(), i->pointsVector());
	finalize(false);
}

void ProfileChart::setCategories(QStringList categories)
{
	auto a = qobject_cast<QtCharts::QBarCategoryAxis*>(ax);
	a->setCategories(categories);
}

void ProfileChart::clear()
{
	stats = {{}, {}};
	content.clear();
	removeAllSeries();
}

void ProfileChart::addSample(QString name, QVector<QPointF> points)
{
	auto i = new QtCharts::QLineSeries;
	i->setName(name);
	i->replace(points);
	content.push_back(i);
}

void ProfileChart::finalize(bool fresh)
{
	if (fresh) {
		computeStats();
		std::sort(content.begin(), content.end(),
		          [] (auto a, auto b) { return a->name() < b->name(); });
	}

	bool reduced = fresh && content.size() >= 25;
	bool outer = (!fresh || reduced) && !stats.mean.empty();

	auto add = [this] (QtCharts::QAbstractSeries *s, bool individual) {
		addSeries(s);
		for (auto a : {ax, ay})
			s->attachAxis(a);
		auto signal = individual ? &ProfileChart::toggleIndividual : &ProfileChart::toggleAverage;
		connect(this, signal, s, &QtCharts::QAbstractSeries::setVisible);
	};

	// setup and add QAreaSeries for stddev
	if (outer) {
		auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			upper->append(i, stats.mean[i] + stats.stddev[i]);
			lower->append(i, stats.mean[i] - stats.stddev[i]);
		}
		auto s = new QtCharts::QAreaSeries(upper, lower);
		add(s, false);
		s->setName("σ (SD)");
		s->setColor(Qt::gray);
		s->setBorderColor(Qt::gray);
	}

	// add individual series
	if (!reduced) {
		for (auto i : content)
			add(i, true);
	}

	// setup and add QLineSeries for mean
	if (outer) {
		auto s = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			s->append(i, stats.mean[i]);
		}
		add(s, false);
		s->setName("Avg.");
		auto pen = s->pen();
		pen.setColor(Qt::black);
		pen.setWidthF(pen.widthF()*1.5);
		s->setPen(pen);
	}
}

void ProfileChart::computeStats()
{
	auto input = content;
	if (input.size() < 2)
		return;

	/* really not the brightest way to do this.
	   might want to indeed convert to sane formats and back to use OpenCV */

	stats.mean.resize((unsigned)input[0]->pointsVector().size());
	for (auto s : input) {
		auto points = s->pointsVector();
		for (unsigned j = 0; j < stats.mean.size(); ++j)
			stats.mean[j] += points[(int)j].y();
	}

	for (auto &v : stats.mean)
		v /= input.size();

	stats.stddev.resize(stats.mean.size());
	for (auto s : input) {
		auto points = s->pointsVector();
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			auto diff = points[(int)i].y() - stats.mean[i];
			stats.stddev[i] += diff*diff;
		}
	}
	for (auto &v : stats.stddev) {
		v /= (input.size() - 1);
		v = std::sqrt(v);
	}
}
