#include "chartview.h"
#include "chart.h"

#include <QtDebug>

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState) {
		qobject_cast<Chart*>(chart())->trackCursor(event->pos());
	}

	QChartView::mouseMoveEvent(event);
}


void ChartView::mousePressEvent(QMouseEvent *event)
{
	QChartView::mousePressEvent(event);
	if (event->isAccepted())
		rubberState = true;
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)
{
	QChartView::mouseReleaseEvent(event);
	if (event->isAccepted())
		rubberState = false;
}

void ChartView::leaveEvent(QEvent *)
{
	qobject_cast<Chart*>(chart())->trackCursor({});
}
