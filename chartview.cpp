#include "chartview.h"
#include "chart.h"

#include <memory>

#include <QtDebug>

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState) {
		qobject_cast<Chart*>(chart())->updateCursor(event->pos());
	}

	QChartView::mouseMoveEvent(event);
}


void ChartView::mousePressEvent(QMouseEvent *event)
{
	QChartView::mousePressEvent(event);
	if (event->isAccepted()) { // sadly always…
		rubberState = true;
		rubberPerformed = false;
		/* terrible hack to find out if the plot changed between click press and
		   release, which means, probably the rubber was active, and we should
		   ignore the next release event */
		auto conn = std::make_shared<QMetaObject::Connection>();
		*conn = connect(qobject_cast<Chart*>(chart()), &Chart::cursorChanged, [this, conn] {
			rubberPerformed = true;
			disconnect(*conn);
		});
	}
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)
{
	QChartView::mouseReleaseEvent(event);
	if (event->isAccepted()) { // sadly always…
		rubberState = false;
	}

	if (rubberPerformed)
		return;

	if (event->button() == Qt::LeftButton) {
		auto c = qobject_cast<Chart*>(chart());
		c->cursorLocked = !c->cursorLocked;
	}
}

void ChartView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	setFocus(Qt::MouseFocusReason);
}

void ChartView::leaveEvent(QEvent *)
{
	qobject_cast<Chart*>(chart())->updateCursor();
}

void ChartView::keyReleaseEvent(QKeyEvent *event)
{
	QChartView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;

	if (event->key() == Qt::Key_Space) {
		auto c = qobject_cast<Chart*>(chart());
		c->cursorLocked = !c->cursorLocked;
	}
}

void ChartView::wheelEvent(QWheelEvent *event)
{
	QChartView::wheelEvent(event);
	if (event->isAccepted())
		return;

	auto factor = 1. + 0.001*event->delta();
	qobject_cast<Chart*>(chart())->zoomAt(mapToScene(event->pos()), factor);
}
