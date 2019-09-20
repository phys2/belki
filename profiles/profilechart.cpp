#include "profilechart.h"
#include "profilewindow.h"
#include "centralhub.h"
#include "proteindb.h"
#include "dataset.h"

#include <QLineSeries>
#include <QAreaSeries>
#include <QValueAxis>
#include <QLogValueAxis>
#include <QCategoryAxis>
#include <QLegendMarker>

#include <opencv2/core/core.hpp>
#include <tbb/parallel_for_each.h>

/* small, inset plot constructor */
ProfileChart::ProfileChart(Dataset::ConstPtr data, bool small)
    : data(data),
      small(small)
{
	if (small) {
		setMargins({0, 10, 0, 0});
		legend()->hide();
	} else {
		legend()->setAlignment(Qt::AlignLeft);
	}
	auto d = data->peek<Dataset::Base>();
	labels = d->dimensions;
	setupAxes(d->featureRange);
	setupSignals();
}

/* big, labelled plot constructor */
ProfileChart::ProfileChart(ProfileChart *source)
    : content(source->content), stats(source->stats),
      data(source->data), labels(source->labels),
      small(false), logSpace(source->logSpace)
{
	setTitle(source->title());
	legend()->setAlignment(Qt::AlignLeft);
	setupAxes({source->ay->min(), source->ay->max()});
	setupSignals();
}

void ProfileChart::setupSignals()
{
	/* keep in sync */
	connect(this, &ProfileChart::toggleAverage, [this] (bool on) { showAverage = on; });
	connect(this, &ProfileChart::toggleIndividual, [this] (bool on) { showIndividual = on; });
}

void ProfileChart::setupAxes(const Features::Range &range)
{
	ax = new QtCharts::QCategoryAxis;
	ax->setRange(0, labels.size() - 1);
	addAxis(ax, Qt::AlignBottom);
	ax->hide();

	if (!small) {
		/* prepare a secondary axis that will show labels when requested.
		 * smoother than show/hiding the primary axis w/ labels */
		axC = new QtCharts::QCategoryAxis;
		axC->setLabelsAngle(-90);
		axC->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
		axC->setRange(ax->min(), ax->max());
		auto i = 0;
		for (auto &l : qAsConst(labels))
			axC->append(l, i++);
	}

	ay = new QtCharts::QValueAxis;
	if (small) {
		ay->setLabelFormat("%.2g");
		auto font = ay->labelsFont();
		font.setPointSizeF(font.pointSizeF()*0.75);
		ay->setLabelsFont(font);
	}
	ay->setRange(range.min, range.max);

	ayL = new QtCharts::QLogValueAxis;
	double lb;
	if (range.max > 10000)
		lb = 1;
	else if (range.max > 100)
		lb = 0.01;
	else if (range.max > 10)
		lb = 0.001;
	else
		lb = 0.0001;
	ayL->setRange(std::max(range.min, lb), std::max(range.max, lb));
	ayL->setBase(10.);
	ayL->setLabelFormat("%.2g");
	ayL->setLabelsFont(ay->labelsFont());

	auto needed = (logSpace ? (QtCharts::QAbstractAxis*)ayL : ay);
	addAxis(needed, small ? Qt::AlignRight : Qt::AlignLeft);
}

void ProfileChart::clear()
{
	stats = {};
	content.clear();
	removeAllSeries();
}

void ProfileChart::addSample(ProteinId id, bool marker)
{
	try {
		auto index = data->peek<Dataset::Base>()->protIndex.at(id);
		content.push_back({index, marker});
	} catch (std::out_of_range&) {}
}

void ProfileChart::addSampleByIndex(unsigned index, bool marker)
{
	content.push_back({index, marker});
}

void ProfileChart::finalize() {
	setupSeries();
}

void ProfileChart::setupSeries()
{
	if (showAverage && stats.mean.empty()) {
		computeStats();
		// if we couldn't compute, just disable. GUI should have it disabled already
		showAverage = !stats.mean.empty();
	}

	auto d = data->peek<Dataset::Base>();
	auto p = data->peek<Dataset::Proteins>();

	std::function<bool(const std::pair<unsigned,bool> &a, const std::pair<unsigned,bool> & b)>
	byName = [&d,&p] (auto a, auto b) {
		return d->lookup(p, a.first).name < d->lookup(p, b.first).name;
	};
	auto byMarkedThenName = [byName] (auto a, auto b) {
		if (a.second != b.second)
			return b.second;
		return byName(a, b);
	};
	// sort by name, but in small view, put marked last (for z-index)
	std::sort(content.begin(), content.end(), small ? byMarkedThenName : byName);

	std::function<double(double)> adjusted;
	if (logSpace)
		adjusted = [&] (qreal v) { return std::max(v, ayL->min()); };
	else
		adjusted = [] (qreal v) { return v; };

	/* prepare adjusted feature points array in log case to speed this up */
	std::vector<QVector<QPointF>> featurePoints;
	if (logSpace) {
		featurePoints = d->featurePoints;
		tbb::parallel_for_each(featurePoints, [&] (QVector<QPointF> &points) {
			for (auto &i : points)
				i.setY(adjusted(i.y()));
		});
	}

	// add & wire a series
	auto add = [this] (QtCharts::QAbstractSeries *s, bool isIndiv, bool isMarker = false) {
		addSeries(s);
		s->attachAxis(ax);
		s->attachAxis(logSpace ? (QtCharts::QAbstractAxis*)ayL : ay);
		if (!isMarker) {// marker always shows
			s->setVisible(isIndiv ? showIndividual : showAverage);
			auto signal = isIndiv ? &ProfileChart::toggleIndividual : &ProfileChart::toggleAverage;
			connect(this, signal, s, &QtCharts::QAbstractSeries::setVisible);
		}
	};

	// setup and add QLineSeries for mean
	auto addMean = [&] {
		auto s = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			s->append(i, adjusted(stats.mean[i]));
		}
		add(s, false);
		s->setName("Avg.");
		auto pen = s->pen();
		pen.setColor(Qt::black);
		pen.setWidthF(pen.widthF()*1.5);
		s->setPen(pen);
	};

	// setup and add QAreaSeries for range and stddev
	auto addBgAreas = [&] {
		auto create = [&] (auto source) {
			auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
			for (unsigned i = 0; i < stats.mean.size(); ++i) {
				auto [u, l] = source(i);
				upper->append(i, adjusted(u));
				lower->append(i, adjusted(l));
			}
			auto s = new QtCharts::QAreaSeries(upper, lower);
			add(s, false);
			return s;
		};

		// create range series (min-max)
		auto s1 = create([&] (unsigned i) -> std::pair<qreal,qreal> {
			return {stats.max[i], stats.min[i]};
		});
		s1->setName("Range");
		auto pen = s1->pen(); // border
		pen.setColor(Qt::lightGray);
		pen.setWidthF(0);
		s1->setPen(pen);
		auto b = s1->brush(); // fill
		b.setColor(Qt::lightGray);
		b.setStyle(Qt::BrushStyle::BDiagPattern);
		s1->setBrush(b);

		// create stddev series
		auto s2 = create([&] (unsigned i) -> std::pair<qreal,qreal> {
			return {stats.mean[i] + stats.stddev[i], stats.mean[i] - stats.stddev[i]};
		});
		s2->setName("σ (SD)");
		s2->setColor(Qt::gray);
		s2->setBorderColor(Qt::gray);
	};

	// add individual series (markers or all)
	auto addIndividuals = [&] (bool onlyMarkers) {
		for (auto [index, isMarker] : content) {
			if (onlyMarkers && !isMarker)
				continue;

			auto s = new QtCharts::QLineSeries;
			series[index] = s;
			add(s, true, isMarker);
			QString title = (isMarker ? "<small>★</small>" : "") + d->lookup(p, index).name;
			// color only markers in small view
			QColor color = (isMarker || !small ? d->lookup(p, index).color : Qt::black);
			if (isMarker && !small) { // acentuate markers in big view
				auto pen = s->pen();
				pen.setWidthF(3. * pen.widthF());
				s->setPen(pen);
			}
			s->setColor(color);
			s->setName(title);

			if (d->hasScores()) { // visualize scores through points along polyline
				s->setPointsVisible(true);
				// note: a copy of the relevant scores is stored in the lambda object
				s->setDynamicPointSize([s=d->scores[index],max=d->scoreRange.max] (int index) {
					return s[(size_t)index] * 3./max;
				});
			}

			s->replace(logSpace ? featurePoints[index] : d->featurePoints[index]);

			/* allow highlight through series/marker hover */
			auto lm = legend()->markers(s).first();
			connect(lm, &QtCharts::QLegendMarker::hovered, [this,i=index] (bool on) {
				toggleHighlight(on ? i : 0);
			});
			connect(s, &QtCharts::QLineSeries::hovered, [this,i=index] (auto, bool on) {
				toggleHighlight(on ? i : 0);
			});
		}
	};

	if (small) {
		/* we show either average or individual. only add what's necessary */
		if (showAverage) {
			addBgAreas();
			addMean();
			addIndividuals(true);
		} else {
			addIndividuals(false);
		}
	} else {
		/* add everything in stacking order, to be toggle-able later on */
		addBgAreas();
		addIndividuals(false);
		addMean();
	}
}

void ProfileChart::toggleHighlight(unsigned index)
{
	highlightAnim.disconnect();
	highlightAnim.callOnTimeout([this, index] {
		bool decrease = (index == 0);
		bool done = true;
		for (auto &[i, s] : series) {
			auto c = s->color();
			if (i == index || decrease) {
				if (c.alphaF() < 1.) {
					c.setAlphaF(std::min(1., c.alphaF() + .15));
					done = false;
				}
			} else {
				if (c.alphaF() > .2) {
					c.setAlphaF(std::max(.2, c.alphaF() - .15));
					done = false;
				}
			}
			s->setColor(c);
		}
		if (done)
			highlightAnim.stop();
	});
	highlightAnim.start(25);
}

void ProfileChart::toggleLabels(bool on) {
	/* It appears smoother/faster to add/remove a secondary axis than to show/hide */
	if (on)
		addAxis(axC, Qt::AlignBottom);
	else
		removeAxis(axC);
}

void ProfileChart::toggleLogSpace(bool on)
{
	if (logSpace == on)
		return;

	logSpace = on;
	removeAllSeries();

	auto needed = (logSpace ? (QtCharts::QAbstractAxis*)ayL : ay);
	auto previous = (logSpace ? (QtCharts::QAbstractAxis*)ay : ayL);
	removeAxis(previous);
	addAxis(needed, previous->alignment());

	if (!content.empty())
		setupSeries();
}

void ProfileChart::computeStats()
{
	if (content.size() < 2)
		return;

	auto d = data->peek<Dataset::Base>();
	auto len = (size_t)d->dimensions.size();

	for (auto v : {&stats.mean, &stats.stddev, &stats.min, &stats.max})
		v->resize(len);

	/* compute statistics per-dimension */
	for (size_t i = 0; i < len; ++i) {
		std::vector<double> f(content.size());
		for (size_t j = 0; j < content.size(); ++j)
			f[j] = d->features[content[j].first][i];
		cv::Scalar m, s;
		cv::meanStdDev(f, m, s);
		stats.mean[i] = m[0];
		stats.stddev[i] = s[0];
		cv::minMaxLoc(f, &stats.min[i], &stats.max[i]);
	}
}
