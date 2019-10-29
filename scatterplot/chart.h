#ifndef CHART_H
#define CHART_H

#include "chartconfig.h"
#include "dataset.h"
#include "utils.h"

#include <QChart>
#include <QScatterSeries>
#include <QStack>

#include <unordered_map>

class QColor;
class QTimer;
class WindowState;

namespace QtCharts {
class QValueAxis;
}

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:

	struct Proteins : QtCharts::QScatterSeries, NonCopyable // registers itself to the chart
	{
		Proteins(const QString &label, QColor color, Chart* chart);

		void clear() { QtCharts::QScatterSeries::clear(); samples.clear(); }
		void add(unsigned index, const QPointF &point);
		void apply();
		void redecorate(bool full = true, bool highlight = false);

		QVector<unsigned> samples;
		QVector<QPointF> replacement;
		bool highlighted = false;
	};

	struct Marker
	{
		Marker(Chart* chart, unsigned sampleIndex, ProteinId id);

		unsigned index; // global index to keep track of orderings
		inline static unsigned nextIndex = 0; // next index, shared between all charts
		unsigned sampleIndex;
		ProteinId sampleId;

		// items are added to the scene, so we make them non-copyable
		// although scene also owns them, we delete first and they de-register
		std::unique_ptr<QtCharts::QScatterSeries> series;

	protected:
	};

	Chart(Dataset::ConstPtr data, const ChartConfig *config);
	void setState(std::shared_ptr<WindowState> s);
	void setConfig(const ChartConfig *config);

	void hibernate();
	void wakeup();

public slots:
	void setTitles(const QString &x, const QString &y);
	void display(const QVector<QPointF> &coords);
	void changeAnnotations();
	void toggleAnnotations();
	void updateMarkers(bool newDisplay = false);
	void toggleMarkers(const std::vector<ProteinId> &ids, bool present);

	void zoomAt(const QPointF &pos, qreal factor);
	void undoZoom(bool full = false);

	void refreshCursor();
	void resetCursor();
	void moveCursor(const QPointF &pos = {});
	void toggleCursorLock();

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void markerToggled(ProteinId id, bool present);
	void areaChanged();
	void proteinStyleUpdated();

protected:
	void updatePartitions();
	void animate(int msec);
	ProteinId findFirstMarker();
	static void updateTicks(QtCharts::QValueAxis *axis);

	/* items in the scene */
	Proteins *master; // owned by chart
	// note partitions are also owned by chart, but we delete first and they de-register
	std::unordered_map<int, std::unique_ptr<Proteins>> partitions;
	std::unordered_map<ProteinId, Marker> markers;
	ProteinId firstMarker = 0; // cached for stack-ordering (0 means none)

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;
	// deferred animation reset
	QTimer *animReset;

	/* GUI state */
	bool awake = false;
	const ChartConfig *config;

	/* data state variables */
	struct {
		QRectF current;
		QStack<QRectF> history;
	} zoom;
	bool cursorLocked = false;
	QPointF cursorCenter;

	// data source
	Dataset::ConstPtr data;
	std::shared_ptr<WindowState> state;
};

#endif /* CHART_H */
