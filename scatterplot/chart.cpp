#include "chart.h"

#include <QAbstractAxis>
#include <QScatterSeries>
#include <QSplineSeries>
#include <QValueAxis>
#include <QLegendMarker>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPolygonF>
#include <QHash>
#include <QTimer>

#include <map>
#include <cmath>

#include <QDebug>

Chart::Chart(Dataset::ConstPtr data) :
    data(data),
    ax(new QtCharts::QValueAxis), ay(new QtCharts::QValueAxis),
    animReset(new QTimer(this))
{
	/* set up general appearance */
	// disable grid animations as a lot of distracting stuff happens there
	// e.g. axis reset (and animate) when changing animation duration…
	setAnimationOptions(SeriesAnimations);
	legend()->setAlignment(Qt::AlignLeft);

	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);

	/* set up master series */
	master = new Proteins("All proteins", Qt::gray, this);

	/* set up tracker ellipse used in track() */
	tracker = new QGraphicsEllipseItem(this);
	tracker->setPen({Qt::red});
	tracker->setZValue(1000);

	/* reset cursor whenever the zoom changes, TODO: why doesn't it work? */
	connect(ax, &QtCharts::QValueAxis::rangeChanged, this, &Chart::resetCursor);
	connect(ay, &QtCharts::QValueAxis::rangeChanged, this, &Chart::resetCursor);

	/* setup signal for range changes */
	// HACK: we expect ay to always be involved, and always update after ax!
	connect(ay, &QtCharts::QValueAxis::rangeChanged, this, &Chart::areaChanged);

	/* setup zoom history */
	connect(this, &Chart::areaChanged, [this] {
		if (zoom.current.isValid())
			zoom.history.push(zoom.current);
		zoom.current = {{ax->min(), ay->min()}, QPointF{ax->max(), ay->max()}};
	});

	/* setup animation reset timer */
	animReset->setSingleShot(true);
	connect(animReset, &QTimer::timeout, [this] {
		setAnimationDuration(1000);
		setAnimationOptions(SeriesAnimations);
	});
}

void Chart::setTitles(const QString &x, const QString &y)
{
	ax->setTitleText(x);
	ay->setTitleText(y);
}

void Chart::clear()
{
	master->clear();
	markers.clear();
	clearPartitions();
}

void Chart::clearPartitions()
{
	partitions.clear();
}

void Chart::display(const QVector<QPointF> &coords)
{
	/* disable fancy transition on full reset */
	animate(master->pointsVector().empty() ? 0 : 1000);

	resetCursor();
	zoom = {};

	/* update point set */
	master->replace(coords);

	/* update ranges cheap & dirty */
	auto bbox = QPolygonF(master->pointsVector()).boundingRect();
	auto offset = bbox.width() * 0.05; // give some breathing space
	bbox.adjust(-offset, -offset, offset, offset);
	ax->setRange(bbox.left(), bbox.right());
	ay->setRange(bbox.top(), bbox.bottom());

	/* update other sets */
	updatePartitions();
	for (auto &[_, m] : markers) {
		m.series->replace(0, master->pointsVector()[(int)m.sampleIndex]);
	}
}

void Chart::updatePartitions()
{
	auto d = data->peek<Dataset::Structure>();
	bool fresh = partitions.empty();

	/* set up partition series */
	if (fresh) {
		if (d->clustering.empty())
			return; // no clusters means nothing more to do!

		animate(0);

		// series needed for soft clustering
		partitions[-2] = std::make_unique<Proteins>("Unlabeled", Qt::gray, this);
		partitions[-1] = std::make_unique<Proteins>("Mixed", Qt::darkGray, this);

		// go through clusters in their designated order
		for (auto i : d->clustering.order) {
			auto &c = d->clustering.clusters.at(i);
			auto s = new Proteins(c.name, c.color, this);
			partitions.try_emplace((int)i, s);
			/* enable profile view updates on legend label hover */
			auto lm = legend()->markers(s)[0];
			connect(lm, &QtCharts::QLegendMarker::hovered, [this, s] (bool active) {
				if (!active)
					return;
				emit cursorChanged(s->samples, s->name());
				for (auto& [_, p] : partitions)
					p->redecorate(false, s == p.get());
			});
		}
	} else {
		for (auto &[_, p] : partitions)
			p->clear();
	}

	/* populate with proteins */
	if (d->clustering.empty())
		return; // no clusters means nothing more to do!

	auto source = master->pointsVector();
	if (source.empty())
		return; // shouldn't happen, but when it does, better not crash

	for (unsigned i = 0; i < d->clustering.memberships.size(); ++i) {
		auto &m = d->clustering.memberships[i];
		int target = -2; // first series, unlabeled
		if (m.size() > 1)
			target++; // second series, mixed
		if (m.size() == 1)
			target = (int)*m.begin();
		partitions[target]->add(i, source[(int)i]);
	}
	// the partitions use deffered addition, which we need to trigger
	for (auto &[_, p] : partitions)
		p->apply();

	if (fresh) {
		/* hide empty series from legend (in case of hard clustering) */
		for (auto s : {partitions[-2].get(), partitions[-1].get()})
			if (s->pointsVector().empty())
				removeSeries(s);

		/* re-order marker series to come up on top of partitions */
		// unfortunately, due to QCharts suckery, we need to re-create them
		for (auto &[_, m] : markers) {
			m.reAdd();
		}
	}
}

void Chart::updateCursor(const QPointF &pos)
{
	if (cursorLocked)
		return;

	if (pos.isNull() || !plotArea().contains(pos)) {
		if (legend()->contains(pos)) // do not interfer with cluster profile view
			return;
		 // disable tracker
		tracker->hide();
		emit cursorChanged({});
		return;
	}

	const qreal radius = 50;

	// find cursor in feature space (center + range)
	auto center = mapToValue(pos);
	auto diff = center - mapToValue(pos + QPointF{radius, 0});
	auto range = QPointF::dotProduct(diff, diff);

	// shape the corresponding ellipse in viewport space
	auto o = std::sqrt(range);
	QPointF offset(o, o);
	auto topLeft = mapToPosition(center - offset);
	auto botRight = mapToPosition(center + offset);
	tracker->setRect({topLeft, botRight});
	tracker->setTransformOriginPoint(center);

	// show tracker
	tracker->show();

	// determine all proteins that fall into the cursor
	QVector<unsigned> list;
	std::set<int> affectedPartitions;
	auto d = data->peek<Dataset::Structure>();
	auto pv = master->pointsVector();
	for (int i = 0; i < pv.size(); ++i) {
		auto diffVec = pv[i] - center;
		if (QPointF::dotProduct(diffVec, diffVec) < range) {
			list << (unsigned)i;
			for (auto m : d->clustering.memberships[(unsigned)i])
				affectedPartitions.insert((int)m);
		}
	}
	d.unlock();

	for (auto& [i, p] : partitions)
		p->redecorate(false, affectedPartitions.count(i));

	emit cursorChanged(list);
}

void Chart::undoZoom(bool full)
{
	if (zoom.history.empty())
		return;

	auto range = (full ? zoom.history.first() : zoom.history.pop());
	if (full)
		zoom.history.clear();
	ax->setRange(range.left(), range.right());
	ay->setRange(range.top(), range.bottom());
	// undo triggered push
	zoom.history.pop();
}

void Chart::toggleSingleMode()
{
	proteinStyle.singleMode = !proteinStyle.singleMode;
	emit proteinStyleUpdated();
}

void Chart::scaleProteins(qreal factor)
{
	proteinStyle.size *= factor;
	emit proteinStyleUpdated();
}

void Chart::switchProteinBorders()
{
	const QVector<Qt::PenStyle> rot{
		Qt::PenStyle::SolidLine, Qt::PenStyle::DotLine, Qt::PenStyle::NoPen};
	proteinStyle.border = rot[(rot.indexOf(proteinStyle.border) + 1) % rot.size()];
	emit proteinStyleUpdated();
}

void Chart::adjustProteinAlpha(qreal adjustment)
{
	if (proteinStyle.singleMode)
		return; // avoid hidden changes
	auto &a = proteinStyle.alpha.reg;
	a = std::min(1., std::max(0., a + adjustment));
	emit proteinStyleUpdated();
}

void Chart::togglePartitions(bool showPartitions)
{
	if (master->isVisible() != showPartitions)
		return;

	master->setVisible(!showPartitions);
	for (auto &[_, p]: partitions)
		p->setVisible(showPartitions);
}

void Chart::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	// TODO: re-initialize [partitions+]markers
}

void Chart::zoomAt(const QPointF &pos, qreal factor)
{
	animate(0);
	auto stretch = 1./factor;
	auto center = mapToValue(pos);

	/* zoom in a way that point under the mouse stays fixed */
	auto dl = center.x() - ax->min(), dr = ax->max() - center.x();
	ax->setRange(center.x() - dl*stretch, center.x() + dr*stretch);

	auto dt = center.y() - ay->min(), db = ay->max() - center.y();
	ay->setRange(center.y() - dt*stretch, center.y() + db*stretch);
}

void Chart::resetCursor()
{
	cursorLocked = false;
	updateCursor();
}

void Chart::toggleMarker(ProteinId id, bool present)
{
	if (present) {
		try {
			markers.try_emplace(id, data->peek<Dataset::Base>()->protIndex.at(id), id, this);
		} catch (...) {}
	} else {
		markers.erase(id);
	}
}

void Chart::animate(int msec) {
	setAnimationDuration(msec);
	if (msec == 0)
		setAnimationOptions(NoAnimation); // yes, this avoids slowdown…
	animReset->start(msec + 1000);
}

Chart::Proteins::Proteins(const QString &label, QColor color, Chart *chart)
{
	setName(label);
	chart->addSeries(this);
	attachAxis(chart->ax);
	attachAxis(chart->ay);

	setColor(color);
	redecorate();

	chart->legend()->markers(this)[0]->setShape(QtCharts::QLegend::MarkerShapeCircle);

	// follow style changes (note: receiver specified for cleanup on delete!)
	connect(chart, &Chart::proteinStyleUpdated, this, [this] {
		redecorate();
	});
}

void Chart::Proteins::add(unsigned index, const QPointF &point)
{
	replacement.append(point); // deferred addition to chart for speed reasons
	samples.append(index);
}

void Chart::Proteins::apply()
{
	replace(replacement);
	replacement.clear();
}

void Chart::Proteins::redecorate(bool full, bool hl)
{
	if (!full && (hl == highlighted))
		return;

	highlighted = hl;

	auto c = reinterpret_cast<Chart*>(chart());
	if (!c)
		return;

	auto &s = c->proteinStyle;
	if (full)
		setMarkerSize(s.size);

	QPen border(s.border);
	auto col = Qt::darkGray;
	if (highlighted) {
		col = Qt::black;
	}
	border.setColor(col);
	border.setStyle(highlighted ? Qt::PenStyle::SolidLine : s.border);
	setPen(border);

	auto fillColor = brush().color();
	auto alpha = s.alpha.reg;
	if (s.singleMode)
		alpha = (highlighted ? s.alpha.hi : s.alpha.lo);
	fillColor.setAlphaF(alpha);
	setColor(fillColor);
}

Chart::Marker::Marker(unsigned sampleIndex, ProteinId id, Chart *chart)
    : sampleIndex(sampleIndex), sampleId(id),
      series(std::make_unique<QtCharts::QScatterSeries>())
{
	auto s = series.get();
	auto label = chart->data->peek<Dataset::Proteins>()->proteins[sampleId].name;
	s->setName(label);

	s->setPointLabelsFormat(label);
	auto f = s->pointLabelsFont(); // increase font size (do it only on creation)
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	s->setPointLabelsFont(f);

	s->append(chart->master->pointsVector()[(int)sampleIndex]);

	setup(chart);
}

void Chart::Marker::reAdd()
{
	auto chart = qobject_cast<Chart*>(series->chart());
	chart->removeSeries(series.get());
	setup(chart);
}

void Chart::Marker::setup(Chart *chart)
{
	auto s = series.get();
	chart->addSeries(s);

	s->attachAxis(chart->ax);
	s->attachAxis(chart->ay);

	s->setBorderColor(Qt::black);
	s->setColor(chart->data->peek<Dataset::Proteins>()->proteins[sampleId].color);
	s->setMarkerShape(QtCharts::QScatterSeries::MarkerShapeRectangle);
	s->setMarkerSize(chart->proteinStyle.size * 1.3333);
	s->setPointLabelsVisible(true);

	/* allow to remove marker by clicking its legend entry */
	auto lm = chart->legend()->markers(s)[0];
	connect(lm, &QtCharts::QLegendMarker::clicked, [chart, this] {
		emit chart->markerToggled(sampleId, false);
	});

	// follow style changes (note: receiver specified for cleanup on delete!)
	s->connect(chart, &Chart::proteinStyleUpdated, s, [chart, s] {
		// we only care about size
		s->setMarkerSize(chart->proteinStyle.size * 1.3333);
	});
}
