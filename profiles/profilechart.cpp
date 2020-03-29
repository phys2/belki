#include "profilechart.h"
#include "profilewindow.h"
#include "datahub.h"
#include "proteindb.h"
#include "dataset.h"
#include "compute/features.h"

#include <QLineSeries>
#include <QAreaSeries>
#include <QValueAxis>
#include <QLogValueAxis>
#include <QCategoryAxis>
#include <QLegendMarker>

#include <opencv2/core/core.hpp>
#include <tbb/parallel_for_each.h>

/* small, inset plot constructor */
ProfileChart::ProfileChart(Dataset::ConstPtr data, bool small, bool global)
    : data(data),
      small(small),
      globalStats(global)
{
	if (small) {
		setMargins({0, 10, 0, 0});
		legend()->hide();
		sort = Sorting::MARKEDTHENNAME; // put marked last (for z-index)
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
	// TODO awkward as hell; reduce to one slot instead of direct connect to series
	// will need to change the logic anyways when we insert/remove series on-demand
	auto toggler = [this] (SeriesCategory cat, bool on) {
		if (on)	showCategories.insert(cat);
		else	showCategories.erase(cat);
	};
	connect(this, &ProfileChart::toggleAverage, [toggler] (bool on) { toggler(SeriesCategory::AVERAGE, on); });
	connect(this, &ProfileChart::toggleIndividual, [toggler] (bool on) { toggler(SeriesCategory::INDIVIDUAL, on); });
	connect(this, &ProfileChart::toggleQuantiles, [toggler] (bool on) { toggler(SeriesCategory::QUANTILE, on); });
}

void ProfileChart::setupAxes(const Features::Range &range)
{
	/* X axis – used for ticks */
	ax = new QtCharts::QValueAxis;
	ax->setRange(0, labels.size() - 1);
	if (small) {
		ax->setTickCount(2);
		ax->setMinorTickCount(labels.size() - 2);
	} else {
		ax->setTickCount(labels.size());
	}
	ax->setLabelsVisible(false);
	// additionally, make labels tiny as they occupy space
	QFont tiny;
	tiny.setPixelSize(1);
	ax->setLabelsFont(tiny);
	addAxis(ax, Qt::AlignBottom);

	if (!small) {
		/* prepare (not add) a secondary axis that will show labels when requested.
		 * see toggleLabels() for more explanation */
		axC = new QtCharts::QCategoryAxis;

		bool sparse = labels.size() > 50;
		int step = labels.size() / 10;
		if (!sparse)
			axC->setLabelsAngle(-90);
		axC->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
		axC->setRange(ax->min(), ax->max());
		auto i = 0;
		for (auto &l : qAsConst(labels)) {
			if (!sparse || i % step == 0 || i == labels.size() - 1)
				axC->append(l, i);
			i++;
		}
	}

	/* Y axis */
	ay = new QtCharts::QValueAxis;
	if (small) {
		ay->setLabelFormat("%.2g");
		auto font = ay->labelsFont();
		font.setPointSizeF(font.pointSizeF()*0.75);
		ay->setLabelsFont(font);
	}
	ay->setRange(range.min, range.max);

	ayL = new QtCharts::QLogValueAxis;
	// use sanitized range for logscale axis
	auto lrange = features::log_valid(range);
	ayL->setRange(lrange.min, lrange.max);
	ayL->setBase(10.);
	ayL->setLabelFormat("%.2g");
	ayL->setLabelsFont(ay->labelsFont());

	addAxis(logSpace ? (QtCharts::QAbstractAxis*)ayL : ay,
	        small ? Qt::AlignRight : Qt::AlignLeft);
}

void ProfileChart::clear()
{
	if (!globalStats)
		stats = {};
	content.clear();
	series.clear();
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
	if (content.empty() && !globalStats)
		return;

	/* TODO: rework this so we only setup what is seen on screen, and use
	 * insertSeries() on toggles. Note that we might also need a legendmarker
	 * sorting functionality.
	 * This is important as it will greatly improve performance when showing
	 * stats-only plots of a huge number of proteins.
	 */

	// compute stats always for large plots, but also when avg. is shown in small plot
	if ((showCategories.count(SeriesCategory::AVERAGE) || !small) && stats.mean.empty()) {
		computeStats();
		// if we couldn't compute, just disable. GUI should have it disabled already
		if (stats.mean.empty())
			showCategories.erase(SeriesCategory::AVERAGE);
		if (stats.quant25.empty())
			showCategories.erase(SeriesCategory::QUANTILE);
	}

	auto d = data->peek<Dataset::Base>();
	auto p = data->peek<Dataset::Proteins>();

	std::map<Sorting,std::function<bool(const std::pair<unsigned,bool> &a, const std::pair<unsigned,bool> &b)>> sorters;
	sorters[Sorting::NAME] = [&d,&p] (auto a, auto b) {
		return d->lookup(p, a.first).name < d->lookup(p, b.first).name;
	};
	sorters[Sorting::MARKEDTHENNAME] = [super=sorters.at(Sorting::NAME)] (auto a, auto b) {
		if (a.second != b.second)
			return b.second;
		return super(a, b);
	};
	if (sort != Sorting::NONE)
		std::sort(content.begin(), content.end(), sorters.at(sort));

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

	// setup and add QLineSeries for mean
	auto addMean = [&] {
		auto s = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			s->append(i, adjusted(stats.mean[i]));
		}
		addSeries(s, SeriesCategory::AVERAGE);
		s->setName("Avg.");
		auto pen = s->pen();
		pen.setColor(Qt::black);
		pen.setWidthF(pen.widthF()*1.5);
		s->setPen(pen);
	};

	// setup and add QAreaSeries for range and stddev
	auto addBgAreas = [&] (const std::set<SeriesCategory> &categories) {
		auto create = [&] (auto source, SeriesCategory cat) {
			auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
			for (unsigned i = 0; i < stats.mean.size(); ++i) {
				auto [u, l] = source(i);
				upper->append(i, adjusted(u));
				lower->append(i, adjusted(l));
			}
			auto s = new QtCharts::QAreaSeries(upper, lower);
			addSeries(s, cat);
			return s;
		};

		// create range series (min-max)
		{
			auto s = create([&] (unsigned i) -> std::pair<qreal,qreal> {
				return {stats.max[i], stats.min[i]};
			}, SeriesCategory::AVERAGE);
			s->setName("Range");
			auto border = s->pen();
			border.setColor(Qt::lightGray);
			border.setWidthF(0);
			s->setPen(border);
			auto fill = s->brush();
			fill.setColor(Qt::lightGray);
			fill.setStyle(Qt::BrushStyle::BDiagPattern);
			s->setBrush(fill);
		}

		if (categories.count(SeriesCategory::AVERAGE)) {
			// create stddev series
			auto s = create([&] (unsigned i) -> std::pair<qreal,qreal> {
				return {stats.mean[i] + stats.stddev[i], stats.mean[i] - stats.stddev[i]};
			}, SeriesCategory::AVERAGE);
			s->setName("σ (SD)");
			s->setColor(Qt::lightGray);
			s->setBorderColor(Qt::lightGray);
		}

		if (categories.count(SeriesCategory::QUANTILE)) {
			// create quantile series
			auto s = create([&] (unsigned i) -> std::pair<qreal,qreal> {
				return {stats.quant25[i], 0.};
			}, SeriesCategory::QUANTILE);
			auto border = s->pen(); // border
			border.setWidthF(border.widthF() * 0.5);
			auto c = QColor(Qt::gray).lighter(90);
			auto fill = s->brush();
			s->setName("Quant. 25");
			fill.setColor(c);
			fill.setStyle(Qt::BrushStyle::Dense5Pattern);
			s->setBrush(fill);
			s->setPen(border);
			s->setBorderColor(c);
			s = create([&] (unsigned i) -> std::pair<qreal,qreal> {
				return {stats.quant50[i], stats.quant25[i]};
			}, SeriesCategory::QUANTILE);
			s->setName("Quant. 50");
			fill = s->brush();
			fill.setColor(c);
			fill.setStyle(Qt::BrushStyle::Dense6Pattern);
			s->setBrush(fill);
			s->setPen(border);
			s->setBorderColor(c);
			s = create([&] (unsigned i) -> std::pair<qreal,qreal> {
				return {stats.quant75[i], stats.quant50[i]};
			}, SeriesCategory::QUANTILE);
			s->setName("Quant. 75");
			fill = s->brush();
			fill.setColor(c);
			fill.setStyle(Qt::BrushStyle::Dense7Pattern);
			s->setBrush(fill);
			s->setPen(border);
			s->setBorderColor(c);
		}
	};

	// add individual series (markers or all)
	auto addIndividuals = [&] (bool onlyMarkers) {
		for (auto [index, isMarker] : content) {
			if (onlyMarkers && !isMarker)
				continue;

			auto id = d->protIds[index];
			auto &prot = p->proteins[id];

			auto s = new QtCharts::QLineSeries;
			series[index] = s;
			addSeries(s, SeriesCategory::INDIVIDUAL, isMarker);
			// color only markers in small view
			if (isMarker && !small) { // acentuate markers in big view
				auto pen = s->pen();
				pen.setWidthF(3. * pen.widthF());
				s->setPen(pen);
			}
			s->setColor(colorOf(index, prot.color, isMarker));
			s->setName(titleOf(index, prot.name, isMarker));

			if (d->hasScores()) { // visualize scores through points along polyline
				s->setPointsVisible(true);
				// note: a copy of the relevant scores is stored in the lambda object
				s->setDynamicPointSize([s=d->scores[index],max=d->scoreRange.max] (int index) {
					return s[(size_t)index] * 3./max;
				});
			}

			s->replace(logSpace ? featurePoints[index] : d->featurePoints[index]);

			auto lm = legend()->markers(s).first();
			if (!isMarker)
				lm->setShape(QtCharts::QLegend::MarkerShape::MarkerShapeCircle);

			/* allow highlight through series/marker hover */
			connect(s, &QtCharts::QLineSeries::hovered, [this,i=index] (auto, bool on) {
				toggleHighlight(on ? (int)i : -1);
			});
			connect(lm, &QtCharts::QLegendMarker::hovered, [this,i=index] (bool on) {
				toggleHighlight(on ? (int)i : -1);
			});

			/* install protein menu  */
			auto openMenu = [this,id] { emit menuRequested(id); };
			connect(s, &QtCharts::QLineSeries::clicked, openMenu);
			connect(lm, &QtCharts::QLegendMarker::clicked, openMenu);
		}
	};

	if (small) {
		/* we show either average or individual. only add what's necessary */
		if (showCategories.count(SeriesCategory::AVERAGE)) {
			addBgAreas({SeriesCategory::AVERAGE});
			addMean();
			addIndividuals(true);
		} else {
			addIndividuals(false);
		}
	} else {
		/* add everything in stacking order, to be toggle-able later on */
		addBgAreas({SeriesCategory::AVERAGE, SeriesCategory::QUANTILE});
		addIndividuals(false);
		addMean();
	}
}

QString ProfileChart::titleOf(unsigned, const QString &name, bool) const
{
	return name;
}

QColor ProfileChart::colorOf(unsigned, const QColor &color, bool isMarker) const
{
	return (isMarker || !small ? color : Qt::black);
}

void ProfileChart::animHighlight(int index, qreal step)
{
	bool decrease = (index < 0);
	bool done = true;
	for (auto &[i, s] : series) {
		auto c = s->color();
		if ((int)i == index || decrease) {
			if (c.alphaF() < 1.) {
				c.setAlphaF(std::min(1., c.alphaF() + step));
				done = false;
			}
		} else {
			if (c.alphaF() > .2) {
				c.setAlphaF(std::max(.2, c.alphaF() - step));
				done = false;
			}
		}
		s->setColor(c);
	}
	if (done)
		highlightAnim.stop();
}

void ProfileChart::toggleHighlight(int index)
{
	highlightAnim.disconnect();
	highlightAnim.callOnTimeout([this,index] { animHighlight(index, .2); });
	highlightAnimDeadline.setRemainingTime(150, Qt::PreciseTimer);
	animHighlight(index, .2);
	/* continue after first drawing update */
	QTimer::singleShot(0, [this,index] {
		/* finish animation early if drawing was too slow */
		if (highlightAnimDeadline.hasExpired())
			animHighlight(index, 1.);
		else
			highlightAnim.start(50);
	});
}

void ProfileChart::toggleLabels(bool on) {
	/* Note: We cannot just use a single axis and set visibility of labels as
	 * the space occupied by the labels is not freed when hiding them. So we use
	 * need to show/hide the full axis (and we need a second one as this would
	 * also take away ticks). It appears smoother/faster to add/remove the
	 * secondary axis though than to simply show/hiding it.
	 */
	if (axes().contains(axC) == on)
		return;
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

	setupSeries();
}

void ProfileChart::computeStats()
{
	if (!globalStats && content.size() < 2)
		return;

	auto d = data->peek<Dataset::Base>();
	auto len = (size_t)d->dimensions.size();

	for (auto v : {&stats.mean, &stats.stddev,
		           &stats.min, &stats.max,
		           &stats.quant25, &stats.quant50, &stats.quant75})
		v->resize(len);

	/* compute statistics per-dimension */
	auto nFeats = globalStats ? d->features.size() : content.size();
	auto statsPerDim = [&] (unsigned dim) {
		std::vector<double> f(nFeats);
		if (globalStats) {
			for (size_t j = 0; j < nFeats; ++j)
				f[j] = d->features[j][dim];
		} else {
			for (size_t j = 0; j < nFeats; ++j)
				f[j] = d->features[content[j].first][dim];
		}

		cv::Scalar m, s;
		cv::meanStdDev(f, m, s);
		stats.mean[dim] = m[0];
		stats.stddev[dim] = s[0];

		//cv::minMaxLoc(f, &stats.min[dim], &stats.max[dim]);
		std::sort(f.begin(), f.end());
		stats.min[dim] = f.front();
		stats.max[dim] = f.back();
		stats.quant25[dim] = f[nFeats / 4];
		stats.quant50[dim] = f[nFeats / 2];
		stats.quant75[dim] = f[(nFeats * 3) / 4];
	};

	if (nFeats < 1000) {
		for (size_t i = 0; i < len; ++i)
			statsPerDim(i);
	} else {
		tbb::parallel_for(size_t(0), len, [&] (size_t i) { statsPerDim(i); });
}
}

void ProfileChart::addSeries(QtCharts::QAbstractSeries *s, ProfileChart::SeriesCategory cat, bool sticky)
{
	QtCharts::QChart::addSeries(s);
	s->attachAxis(ax);
	s->attachAxis(logSpace ? (QtCharts::QAbstractAxis*)ayL : ay);
	if (sticky || cat == SeriesCategory::CUSTOM) // always shows
		return;
	s->setVisible(showCategories.count(cat));
	std::map<SeriesCategory, void(ProfileChart::*)(bool)> sigs = {
		{SeriesCategory::INDIVIDUAL, &ProfileChart::toggleIndividual},
		{SeriesCategory::AVERAGE, &ProfileChart::toggleAverage},
		{SeriesCategory::QUANTILE, &ProfileChart::toggleQuantiles},
	};
	connect(this, sigs.at(cat), s, &QtCharts::QAbstractSeries::setVisible);
}
