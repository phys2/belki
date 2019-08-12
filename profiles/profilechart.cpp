#include "profilechart.h"
#include "profilewindow.h"
#include "centralhub.h"
#include "proteindb.h"
#include "dataset.h"

#include <QLineSeries>
#include <QAreaSeries>
#include <QValueAxis>
#include <QCategoryAxis>

#include <opencv2/core/core.hpp>


/* small, inset plot constructor */
ProfileChart::ProfileChart(Dataset::ConstPtr data)
    : data(data)
{
	setMargins({0, 10, 0, 0});
	legend()->hide();
	auto d = data->peek<Dataset::Base>();
	setupAxes(d->featureRange, d->dimensions, true);
}

/* big, labelled plot constructor */
ProfileChart::ProfileChart(ProfileChart *source)
    : content(source->content), stats(source->stats),
      data(source->data)
{
	setTitle(source->title());
	legend()->setAlignment(Qt::AlignLeft);

	setupAxes({source->ay->min(), source->ay->max()}, source->ax->categoriesLabels(), false);

	/* Series are non-copyable, so we just recreate them */
	finalize(false);
}

void ProfileChart::setupAxes(const Features::Range &range, const QStringList &labels, bool small)
{
	ax = new QtCharts::QCategoryAxis;
	ax->setLabelsAngle(-90);
	ax->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
	ax->setRange(0, labels.size() - 1);
	addAxis(ax, Qt::AlignBottom);
	if (small)
		ax->hide();

	ay = new QtCharts::QValueAxis;
	if (small) {
		ay->setLabelFormat("%.2g");
		auto font = ay->labelsFont();
		font.setPointSizeF(font.pointSizeF()*0.75);
		ay->setLabelsFont(font);
	}
	ay->setRange(range.min, range.max);
	addAxis(ay, small ? Qt::AlignRight : Qt::AlignLeft);

	/* setup the hide/show feature for labels. in small view show them (but axis
	 * is hidden), so that they are kept for future reference */
	auto toggleLabels = [this,labels] (bool on) {
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
	toggleLabels(small); // enable in small view (to keep them), but disable in large
	connect(this, &ProfileChart::toggleLabels, toggleLabels);
}

void ProfileChart::clear()
{
	stats = {};
	content.clear();
	removeAllSeries();
}

void ProfileChart::addSample(unsigned index, bool marker)
{
	content.push_back({index, marker});
}

void ProfileChart::finalize(bool fresh)
{
	auto d = data->peek<Dataset::Base>();
	if (fresh)
		computeStats();

	bool reduced = fresh && content.size() >= 25;
	bool outer = (!fresh || reduced) && haveStats();

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
	std::sort(content.begin(), content.end(), fresh ? byMarkedThenName : byName);

	// add & wire a series
	auto add = [this] (QtCharts::QAbstractSeries *s, bool isIndiv, bool isMarker = false) {
		addSeries(s);
		for (auto a : std::vector<QtCharts::QAbstractAxis*>{ax, ay})
			s->attachAxis(a);
		auto signal = isIndiv ? &ProfileChart::toggleIndividual : &ProfileChart::toggleAverage;
		if (!isMarker) // marker always shows
			connect(this, signal, s, &QtCharts::QAbstractSeries::setVisible);
	};

	// setup and add QLineSeries for mean
	auto addMean = [&] {
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
	};

	// setup and add QAreaSeries for range and stddev
	auto addBgAreas = [&] {
		auto create = [&] (auto source) {
			auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
			for (unsigned i = 0; i < stats.mean.size(); ++i) {
				auto [u, l] = source(i);
				upper->append(i, u);
				lower->append(i, l);
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

	// add individual series (in order, after area series)
	auto addIndividuals = [&] (bool onlyMarkers) {
		for (auto [index, isMarker] : content) {
			if (onlyMarkers && !isMarker)
				continue;

			auto s = new QtCharts::QLineSeries;
			add(s, true, isMarker);
			QString title = (isMarker ? "<small>★</small>" : "") + d->lookup(p, index).name;
			// color only markers in small view
			QColor color = (isMarker || !fresh ? d->lookup(p, index).color : Qt::black);
			if (isMarker && !fresh) { // acentuate markers in big view
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

			s->replace(d->featurePoints[index]);
		}
	};

	/* add everything in stacking order, based on conditions */
	if (outer) {
		addBgAreas();
	}
	if (!reduced)
		addIndividuals(false);
	if (outer)
		addMean();
	if (reduced)
		addIndividuals(true);
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