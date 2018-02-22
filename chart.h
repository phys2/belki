#ifndef CHART_H
#define CHART_H

#include "dataset.h"

#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QStack>

class QColor;

namespace QtCharts {
class QValueAxis;
}

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:

	struct Proteins : QtCharts::QScatterSeries {
		Proteins(const QString &label, QColor color, Chart* chart);
	};
	struct Marker : QtCharts::QScatterSeries {
		Marker(unsigned sampleIndex, Chart* chart);
		unsigned sampleIndex;
	};

	Chart(Dataset &data);
    virtual ~Chart();

	void display(const QString& set, bool fullReset = false);
	void updatePartitions(bool fullReset = false);
	void addMarker(unsigned sampleIndex);
	void removeMarker(unsigned sampleIndex);
	void clearMarkers();

	void zoomAt(const QPointF &pos, qreal factor);

	static QColor tableau20(unsigned index);

	bool cursorLocked = false;

public slots:
	void resetCursor();
	void updateCursor(const QPointF &pos = {});
	void undoZoom();
	void togglePartitions(bool showPartitions);

signals:
	void cursorChanged(QVector<unsigned> samples);
	void markerToggled(unsigned sampleIndex, bool present);
	void markersCleared();

protected:
	Dataset &data;

	QtCharts::QScatterSeries *master;
	std::vector<QtCharts::QScatterSeries*> partitions;
	QMap<unsigned, Marker*> markers;

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;

	struct {
		QRectF current;
		QStack<QRectF> history;
	} zoom;
};

#endif /* CHART_H */
