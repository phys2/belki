#include "chart.h"

#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPolygonF>
#include <cmath>

#include <QtCore/QDebug>

Chart::Chart(Dataset &data) :
    data(data),
    ax(new QtCharts::QValueAxis), ay(new QtCharts::QValueAxis)
{
	/* set up general appearance */
	setAnimationOptions(QChart::NoAnimation);
	legend()->setAlignment(Qt::AlignLeft);

	setAxisX(ax);
	setAxisY(ay);
	ax->setTitleText("dim 1");
	ay->setTitleText("dim 2");

	/* set up master series */
	master = new QtCharts::QScatterSeries;
	this->addSeries(master);
	master->attachAxis(ax);
	master->attachAxis(ay);
	master->setName("All proteins");

	auto c = master->color();
	master->setBorderColor(c);
	c.setAlphaF(0.5);
	master->setColor(c);

	legend()->markers(master)[0]->setShape(QtCharts::QLegend::MarkerShapeCircle);

	/* set up tracker ellipse used in track() */
	tracker = new QGraphicsEllipseItem(this);
	tracker->setPen({Qt::red});
	tracker->setZValue(50);
	connect(ax, &QtCharts::QValueAxis::rangeChanged, this, &Chart::resetCursor);
	connect(ay, &QtCharts::QValueAxis::rangeChanged, this, &Chart::resetCursor);

	/* setup zoom history */
	// HACK: we expect ay to always be involved, and always update after ax!
	connect(ay, &QtCharts::QValueAxis::rangeChanged, [this] {
		if (zoom.current.isValid())
			zoom.history.push(zoom.current);
		zoom.current = {{ax->min(), ay->min()}, QPointF{ax->max(), ay->max()}};
	});
}

Chart::~Chart()
{
}

void Chart::display(const QString &set, bool fullReset)
{
	// reset state
	if (fullReset) { // completely new data
		clearMarkers();
	}
	resetCursor();
	zoom = {};

	// update point set
	master->replace(data.peek()->display[set]);

	// update ranges cheap & dirty
	auto bbox = QPolygonF(master->pointsVector()).boundingRect();
	auto offset = bbox.width() * 0.05; // give some breathing space
	bbox.adjust(-offset, -offset, offset, offset);
	ax->setRange(bbox.left(), bbox.right());
	ay->setRange(bbox.top(), bbox.bottom());

	// update everything else (should do nothing on reset)
	for (auto m : qAsConst(markers)) {
		m->replace(0, master->pointsVector()[(int)m->sampleIndex]);
	}
}

void Chart::updateCursor(const QPointF &pos)
{
	if (cursorLocked)
		return;

	if (pos.isNull() || !plotArea().contains(pos)) {
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
	auto p = master->pointsVector();
	QVector<unsigned> list;
	for (int i = 0; i < p.size(); ++i) {
		auto diff = p[i] - center;
		if (QPointF::dotProduct(diff, diff) < range) {
			list << (unsigned)i;
		}
	}
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

void Chart::zoomAt(const QPointF &pos, qreal factor)
{
	auto stretch = 1./factor;
	auto center = mapToValue(pos);

	// zoom in a way that point under the mouse stays fixed
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

	/* allow to remove marker by clicking its legend entry */
	auto lm = legend()->markers(markers[sampleIndex])[0];
	connect(lm, &QtCharts::QLegendMarker::clicked, [this, sampleIndex] {
		removeMarker(sampleIndex);
	});
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

QColor Chart::tableau20(bool reset)
{
	const std::vector<QColor> tableau = {
	    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
	    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
	    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
	    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
	    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}};

	static unsigned index = 2;
	if (reset)
		index = 1;
	return tableau[index++ % tableau.size()];
}

Chart::Marker::Marker(unsigned sampleIndex, Chart *chart)
    : sampleIndex(sampleIndex)
{
	auto label = chart->data.peek()->proteins[sampleIndex].firstName;
	setName(label);
	setPointLabelsFormat(label); // displays name over marker point
	append(chart->master->pointsVector()[(int)sampleIndex]);
	chart->addSeries(this);

	attachAxis(chart->axisX());
	attachAxis(chart->axisY());

	setBorderColor(Qt::black);
	setColor(chart->tableau20());
	setMarkerShape(QtCharts::QScatterSeries::MarkerShapeRectangle);
	setMarkerSize(20);
	setPointLabelsVisible(true);
	auto f = pointLabelsFont();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	setPointLabelsFont(f);
}
