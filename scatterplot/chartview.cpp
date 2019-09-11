#include "chartview.h"
#include "chart.h"

Chart *ChartView::chart()
{
	return qobject_cast<Chart*>(QChartView::chart());
}

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState) {
		chart()->moveCursor(event->pos());
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
	chart()->moveCursor();
}

void ChartView::keyReleaseEvent(QKeyEvent *event)
{
	QChartView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;

	if (event->key() == Qt::Key_Space)
		chart()->cursorLocked = !chart()->cursorLocked;

	if (event->key() == Qt::Key_Z)
		chart()->undoZoom(event->modifiers() & Qt::ShiftModifier);

	if (event->key() == Qt::Key_S)
		chart()->toggleSingleMode();

	auto adjustAlpha = [this] (bool decr) {
		chart()->adjustProteinAlpha(decr ? -.05 : .05);
	};
	auto adjustScale = [this] (bool decr) {
		chart()->scaleProteins(decr ? 0.8 : 1.25);
	};
	auto adjustCursor = [this] (bool decr) {
		chart()->scaleCursor(decr ? 0.8 : 1.25);
	};

	if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Minus) {
		if (event->modifiers() & Qt::AltModifier)
			adjustAlpha(event->key() == Qt::Key_Minus);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustScale(event->key() == Qt::Key_Minus);
		else
			adjustCursor(event->key() == Qt::Key_Minus);
	}

	if (event->key() == Qt::Key_B)
		chart()->switchProteinBorders();
}

void ChartView::wheelEvent(QWheelEvent *event)
{
	QChartView::wheelEvent(event);
	if (event->isAccepted())
		return;

	auto factor = [&] (qreal strength) { return 1. + 0.001*strength*event->delta(); };

	if (event->modifiers() & Qt::ControlModifier)
		chart()->scaleCursor(factor(2.));
	else
		chart()->zoomAt(mapToScene(event->pos()), factor(1.));
}
