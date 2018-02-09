#include "chartview.h"
#include "chart.h"

#include <QtDebug>

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState && !cursorLocked) {
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

void ChartView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	setFocus(Qt::MouseFocusReason);
}

void ChartView::leaveEvent(QEvent *)
{
	if (!cursorLocked)
		qobject_cast<Chart*>(chart())->trackCursor({});
}

void ChartView::keyReleaseEvent(QKeyEvent *event)
{
	bool taken = false;
	if (event->key() == Qt::Key_Space) {
		cursorLocked = !cursorLocked;
		taken = true;
	}

	if (!taken)
		QChartView::keyPressEvent(event);
}

void ChartView::wheelEvent(QWheelEvent *event)
{
	QChartView::wheelEvent(event);
	if (event->isAccepted())
		return;

	chart()->zoom(1. + 0.001*event->delta());
}
