#ifndef CHART_H
#define CHART_H

#include "dataset.h"

#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QtCore/QStack>

class QColor;
class QTimer;

namespace QtCharts {
class QValueAxis;
}

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:

	struct Proteins : QtCharts::QScatterSeries {
		Proteins(const QString &label, QColor color, Chart* chart);
		void add(unsigned index, const QPointF &point);
		QVector<unsigned> samples;
	};
	struct Marker : QtCharts::QScatterSeries {
		Marker(unsigned sampleIndex, Chart* chart);
		// no copies/moves! registers itself to the chart in above constructor
		Marker(const Marker&) = delete;
		Marker& operator=(const Marker&) = delete;
		unsigned sampleIndex;
	};

	Chart(Dataset &data);
    virtual ~Chart();

	void clear();
	void clearPartitions();
	void display(const QString& set);
	void updatePartitions();
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
	void areaChanged();
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void markerToggled(unsigned sampleIndex, bool present);
	void markersCleared();

protected:
	void animate(int msec);

	Dataset &data;

	Proteins *master;
	std::vector<Proteins*> partitions;
	QMap<unsigned, Marker*> markers;

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;

	struct {
		QRectF current;
		QStack<QRectF> history;
	} zoom;
	QTimer *animReset;
};

#endif /* CHART_H */
