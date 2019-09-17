#ifndef CHARTVIEW_H
#define CHARTVIEW_H
#include "chartconfig.h"

#include <QChartView>

class Chart;

class ChartView : public QtCharts::QChartView
{
public:
	explicit ChartView(QWidget *parent = nullptr);
	const ChartConfig *getConfig() { return &config; }
	void switchChart(Chart *chart);
	void releaseChart();

public slots:
	void toggleOpenGL(bool enabled);

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	Chart *chart();

	void toggleSingleMode();
	void scaleProteins(qreal factor);
	void switchProteinBorders();
	void adjustProteinAlpha(qreal adjustment);
	void scaleCursor(qreal factor);

	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

	ChartConfig config; // to be shared with current chart

	// workarounds to left-mouse conflict
	bool rubberState = false;
	bool rubberPerformed = false;
};

#endif // CHARTVIEW_H
