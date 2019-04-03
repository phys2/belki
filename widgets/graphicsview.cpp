#include "graphicsview.h"
#include "graphicsscene.h"

#include <QWheelEvent>
#include <cmath>

GraphicsScene *GraphicsView::scene() const
{
	return qobject_cast<GraphicsScene*>(QGraphicsView::scene());
}

void GraphicsView::wheelEvent(QWheelEvent *event)
{
	auto anchor = transformationAnchor();
	setTransformationAnchor(AnchorUnderMouse);
	auto angle = event->angleDelta().y();
	qreal factor = std::pow(1.2, angle / 240.0);
	scale(factor, factor);
	setTransformationAnchor(anchor);
}

void GraphicsView::resizeEvent(QResizeEvent *event)
{
	fitInView(sceneRect(), Qt::KeepAspectRatio);
	QGraphicsView::resizeEvent(event);
}

void GraphicsView::paintEvent(QPaintEvent *event)
{
	auto vp = std::make_pair(viewportTransform(), viewport()->size());
	if (vp != lastViewport) {
		lastViewport = vp;
		auto rect = mapToScene({{0, 0}, viewport()->size()}).boundingRect();
		auto scale = mapToScene(QPoint{1, 1}).x() - rect.left();
		scene()->setViewport(rect, scale);
	}
	QGraphicsView::paintEvent(event);
}
