#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include <QChartView>

class Chart;

class ChartView : public QtCharts::QChartView
{
public:
	using QChartView::QChartView;

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	Chart *chart();

	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

	// workarounds to left-mouse conflict
	bool rubberState = false;
	bool rubberPerformed;
};

#endif // CHARTVIEW_H
