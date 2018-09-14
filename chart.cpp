#include "chart.h"

#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsSceneMouseEvent>
#include <QtGui/QPolygonF>
#include <QtCore/QHash>
#include <QtCore/QTimer>

#include <cmath>

#include <QtCore/QDebug>

Chart::Chart(Dataset &data) :
    data(data),
    ax(new QtCharts::QValueAxis), ay(new QtCharts::QValueAxis),
    animReset(new QTimer(this))
{
	/* set up general appearance */
	// disable grid animations as a lot of distracting stuff happens there
	// e.g. axis reset (and animate) when changing animation duration…
	setAnimationOptions(SeriesAnimations);
	legend()->setAlignment(Qt::AlignLeft);

	setAxisX(ax);
	setAxisY(ay);
	ax->setTitleText("dim 1");
	ay->setTitleText("dim 2");

	/* set up master series */
	master = new Proteins("All proteins", Qt::gray, this);

	/* set up tracker ellipse used in track() */
	tracker = new QGraphicsEllipseItem(this);
	tracker->setPen({Qt::red});
	tracker->setZValue(1000);
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

Chart::~Chart()
{
}

void Chart::clear()
{
	master->clear();
	clearMarkers();
	clearPartitions();
}

void Chart::clearPartitions()
{
	for (auto s : partitions)
		delete s.second;
	partitions.clear();
}

void Chart::display(const QString &set)
{
	/* disable fancy transition on full reset */
	animate(master->pointsVector().empty() ? 0 : 1000);

	resetCursor();
	zoom = {};

	/* update point set */
	master->replace(data.peek()->display[set]);

	/* update ranges cheap & dirty */
	auto bbox = QPolygonF(master->pointsVector()).boundingRect();
	auto offset = bbox.width() * 0.05; // give some breathing space
	bbox.adjust(-offset, -offset, offset, offset);
	ax->setRange(bbox.left(), bbox.right());
	ay->setRange(bbox.top(), bbox.bottom());

	/* update other sets */
	updatePartitions();
	for (auto m : qAsConst(markers)) {
		m->replace(0, master->pointsVector()[(int)m->sampleIndex]);
	}
}

void Chart::updatePartitions()
{
	auto d = data.peek();
	bool fresh = partitions.empty();

	/* set up partition series */
	if (fresh) {
		if (d->clustering.empty())
			return; // no clusters means nothing more to do!

		animate(0);

		// series needed for soft clustering
		partitions[-2] = (new Proteins("Unlabeled", Qt::darkGray, this));
		partitions[-1] = (new Proteins("Mixed", Qt::gray, this));

		unsigned colorCounter = 0;
		for (auto c : d->clustering) {
			auto s = new Proteins(c.second.name, tableau20(colorCounter++), this);
			partitions[(int)c.first] = s;
			/* enable profile view updates on legend label hover */
			auto lm = legend()->markers(s)[0];
			connect(lm, &QtCharts::QLegendMarker::hovered, [this, s] (bool active) {
				if (!active)
					return
				emit cursorChanged(s->samples, s->name());
				for (auto i : partitions)
					i.second->redecorate(false, s == i.second);
			});
		}
	} else {
		for (auto s : partitions)
			s.second->clear();
	}

	/* populate with proteins */
	if (d->clustering.empty())
		return; // no clusters means nothing more to do!

	auto source = master->pointsVector();
	if (source.empty())
		return; // shouldn't happen, but when it does, better not crash

	for (unsigned i = 0; i < d->proteins.size(); ++i) {
		auto &p = d->proteins[i];
		int target = -2; // first series, unlabeled
		if (p.memberOf.size() > 1)
			target++; // second series, mixed
		if (p.memberOf.size() == 1)
			target = (int)*p.memberOf.begin();
		partitions[target]->add(i, source[(int)i]);
	}
	// the partitions use deffered addition, which we need to trigger
	for (auto p : partitions)
		p.second->apply();

	if (fresh) {
		/* hide empty series from legend (in case of hard clustering) */
		for (auto s : {partitions[-2], partitions[-1]})
			if (s->pointsVector().empty())
				removeSeries(s);

		/* re-order marker series to come up on top of partitions */
		// unfortunately, due to QCharts suckery, we need to re-create them
		for (auto i = markers.begin(); i != markers.end(); ++i) {
			delete i.value();
			i.value() = new Marker(i.key(), this);
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
	{
		auto d = data.peek();
		auto p = master->pointsVector();
		for (int i = 0; i < p.size(); ++i) {
			auto diff = p[i] - center;
			if (QPointF::dotProduct(diff, diff) < range) {
				list << (unsigned)i;
				for (auto m : d->proteins[(unsigned)i].memberOf)
					affectedPartitions.insert((int)m);
			}
		}
	} // peek() scope

	for (auto i : partitions)
		i.second->redecorate(false, affectedPartitions.count(i.first));

	emit cursorChanged(list);
}

void Chart::undoZoom()
{
	if (zoom.history.empty())
		return;

	auto range = zoom.history.pop();
	axisX()->setRange(range.left(), range.right());
	axisY()->setRange(range.top(), range.bottom());
	// undo triggered push
	zoom.history.pop();
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
	auto &a = proteinStyle.alpha;
	a = std::min(1., std::max(0., a + adjustment));
	emit proteinStyleUpdated();
}

void Chart::togglePartitions(bool showPartitions)
{
	if (master->isVisible() != showPartitions)
		return;

	master->setVisible(!showPartitions);
	for (auto s: partitions)
		s.second->setVisible(showPartitions);
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

void Chart::addMarker(unsigned sampleIndex)
{
	if (markers.contains(sampleIndex))
		return; // already there

	markers[sampleIndex] = new Marker(sampleIndex, this);

	emit markerToggled(sampleIndex, true);
}

void Chart::removeMarker(unsigned sampleIndex)
{
	if (!markers.contains(sampleIndex))
		return; // already gone

	delete markers[sampleIndex];
	markers.remove(sampleIndex);
	emit markerToggled(sampleIndex, false);
}

void Chart::clearMarkers()
{
	qDeleteAll(markers);
	markers.clear();
	emit markersCleared();
}

void Chart::animate(int msec) {
	setAnimationDuration(msec);
	if (msec == 0)
		setAnimationOptions(NoAnimation); // yes, this avoids slowdown…
	animReset->start(msec + 1000);
}

QColor Chart::tableau20(unsigned index)
{
	const std::vector<QColor> tableau = {
	    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
	    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
	    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
	    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
	    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}};

	return tableau[index % tableau.size()];
}

Chart::Proteins::Proteins(const QString &label, QColor color, Chart *chart)
{
	auto &s = chart->proteinStyle;
	setName(label);
	chart->addSeries(this);
	attachAxis(chart->axisX());
	attachAxis(chart->axisY());

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
	border.setColor(highlighted ? Qt::black : Qt::darkGray);
	border.setStyle(highlighted ? Qt::PenStyle::SolidLine : s.border);
	setPen(border);

	if (!full)
		return;

	auto fillColor = brush().color();
	fillColor.setAlphaF(s.alpha);
	setColor(fillColor);
}

Chart::Marker::Marker(unsigned sampleIndex, Chart *chart)
    : sampleIndex(sampleIndex)
{
	auto label = chart->data.peek()->proteins[sampleIndex].name;
	setName(label);
	setPointLabelsFormat(label); // displays name over marker point
	append(chart->master->pointsVector()[(int)sampleIndex]);
	chart->addSeries(this);

	attachAxis(chart->axisX());
	attachAxis(chart->axisY());

	setBorderColor(Qt::black);
	setColor(chart->tableau20(qHash(label)));
	setMarkerShape(QtCharts::QScatterSeries::MarkerShapeRectangle);
	setMarkerSize(chart->proteinStyle.size * 1.3333);
	setPointLabelsVisible(true);
	auto f = pointLabelsFont();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	setPointLabelsFont(f);

	/* allow to remove marker by clicking its legend entry */
	auto lm = chart->legend()->markers(this)[0];
	connect(lm, &QtCharts::QLegendMarker::clicked, [chart, sampleIndex] {
		chart->removeMarker(sampleIndex);
	});

	// follow style changes (note: receiver specified for cleanup on delete!)
	connect(chart, &Chart::proteinStyleUpdated, this, [chart,this] {
		// we only care about size
		setMarkerSize(chart->proteinStyle.size * 1.3333);
	});
}
