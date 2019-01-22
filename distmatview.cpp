#include "distmatview.h"
#include "distmatscene.h"

#include <QKeyEvent>

#include <QtDebug>

DistmatScene *DistmatView::scene() const
{
	return qobject_cast<DistmatScene*>(QGraphicsView::scene());
}

void DistmatView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	// TODO: needed in Heatmap view?
	setFocus(Qt::MouseFocusReason);
}

void DistmatView::keyReleaseEvent(QKeyEvent *event)
{
	QGraphicsView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;
}

void DistmatView::wheelEvent(QWheelEvent *event)
{
	auto anchor = transformationAnchor();
	setTransformationAnchor(AnchorUnderMouse);
	auto angle = event->angleDelta().y();
	qreal factor = (angle > 0 ? 1.1 : 0.9);
	scale(factor, factor);
	setTransformationAnchor(anchor);
}

void DistmatView::resizeEvent(QResizeEvent *event)
{
	// TODO: when we have annotations etc, it might be wiser to do it the other
	// way round; scale dist matrix to fit in minus margins
	fitInView(sceneRect(), Qt::KeepAspectRatio);
	QGraphicsView::resizeEvent(event);
}

void DistmatView::paintEvent(QPaintEvent *event)
{
	auto vp = std::make_pair(viewportTransform(), viewport()->size());
	if (vp != lastViewport) {
		lastViewport = vp;
		auto t = vp.first.inverted();
		auto rect = mapToScene({{0, 0}, viewport()->size()}).boundingRect();
		auto scale = mapToScene(QPoint{1, 1}).x() - rect.left();
		scene()->setViewport(rect, scale);
		// TODO re-arrange text widgets
	}
	QGraphicsView::paintEvent(event);
}
