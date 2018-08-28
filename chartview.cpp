#include "chartview.h"
#include "chart.h"

#include <memory>

#include <QtDebug>

Chart *ChartView::chart()
{
	return qobject_cast<Chart*>(QChartView::chart());
}

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState) {
		chart()->updateCursor(event->pos());
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
		*conn = connect(chart(), &Chart::areaChanged, [this, conn] {
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
		chart()->cursorLocked = !chart()->cursorLocked;
	}
}

void ChartView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	setFocus(Qt::MouseFocusReason);
}

void ChartView::leaveEvent(QEvent *)
{
	chart()->updateCursor();
}

void ChartView::keyReleaseEvent(QKeyEvent *event)
{
	QChartView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;

	if (event->key() == Qt::Key_Space)
		chart()->cursorLocked = !chart()->cursorLocked;

	if (event->key() == Qt::Key_Z)
		chart()->undoZoom();

	if (event->modifiers() & Qt::AltModifier) {
		if (event->key() == Qt::Key_Plus)
			chart()->adjustProteinAlpha(.05);
		if (event->key() == Qt::Key_Minus)
			chart()->adjustProteinAlpha(-.05);
	} else {
		if (event->key() == Qt::Key_Plus)
			chart()->scaleProteins(1.25);
		if (event->key() == Qt::Key_Minus)
			chart()->scaleProteins(0.8);
	}
	if (event->key() == Qt::Key_B)
		chart()->switchProteinBorders();
}

void ChartView::wheelEvent(QWheelEvent *event)
{
	QChartView::wheelEvent(event);
	if (event->isAccepted())
		return;

	auto factor = 1. + 0.001*event->delta();
	chart()->zoomAt(mapToScene(event->pos()), factor);
}
