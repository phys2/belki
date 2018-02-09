#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include <QtCharts/QChartView>

class ChartView : public QtCharts::QChartView
{
public:
	using QChartView::QChartView;

protected:
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;

	bool rubberState = false;
	bool cursorLocked = false;
};

#endif // CHARTVIEW_H
