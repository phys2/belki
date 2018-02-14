#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include <QtCharts/QChartView>

class Chart;

class ChartView : public QtCharts::QChartView
{
public:
	using QChartView::QChartView;

	// hack override to cast to right type.
	// Note: only works with object refence (e.g. internally) because non-virtual
	Chart *chart();

protected:
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

	bool rubberState = false;
	bool rubberPerformed;
};

#endif // CHARTVIEW_H
