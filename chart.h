#ifndef CHART_H
#define CHART_H

#include "dataset.h"

#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>

namespace QtCharts {
class QValueAxis;
}

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:

	struct Marker : QtCharts::QScatterSeries {
		Marker(int sampleIndex, Chart* chart);
		int sampleIndex;
	};


	Chart(QGraphicsItem *parent = nullptr, Qt::WindowFlags wFlags = nullptr);
    virtual ~Chart();

	void setMeta(QVector<Dataset::Protein> *p) { proteins = p; }

	void display(const QVector<QPointF> &points, bool reset = false);
	void addMarker(int sampleIndex);
	void removeMarker(int sampleIndex);
	void clearMarkers();

	void zoomAt(const QPointF &pos, qreal factor);

	static QColor tableau20(bool reset = false);

	bool cursorLocked = false;

public slots:
	void resetCursor();
	void updateCursor(const QPointF &pos = {});

signals:
	void cursorChanged(QVector<int> samples);
	void markerToggled(int sampleIndex, bool present);
	void markersCleared();

protected:
	QtCharts::QScatterSeries *master;
	std::vector<QtCharts::QScatterSeries*> partitions;
	QMap<int, Marker*> markers;

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;

	QVector<Dataset::Protein> *proteins = nullptr;
};

#endif /* CHART_H */
