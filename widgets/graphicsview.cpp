#include "graphicsview.h"
#include "graphicsscene.h"

#include <QWheelEvent>
#include <cmath>

GraphicsScene *GraphicsView::scene() const
{
	return qobject_cast<GraphicsScene*>(QGraphicsView::scene());
}

void GraphicsView::switchScene(GraphicsScene *newScene)
{
	auto oldScene = scene();
	if (oldScene)
		oldScene->hibernate();

	if (isVisible())
		newScene->wakeup();
	setScene(newScene);
}

void GraphicsView::showEvent(QShowEvent *event)
{
	if (scene())
		scene()->wakeup();
	QGraphicsView::showEvent(event);
}

void GraphicsView::hideEvent(QHideEvent *event)
{
	if (scene())
		scene()->hibernate();
	QGraphicsView::hideEvent(event);
}

void GraphicsView::wheelEvent(QWheelEvent *event)
{
	if (!scrollingEnabled)
		return;

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
	auto lastVp = lastViewport.find(scene());
	if (lastVp == lastViewport.end() || vp != lastVp->second) {
		lastViewport[scene()] = vp;
		auto rect = mapToScene({{0, 0}, viewport()->size()}).boundingRect();
		auto scale = mapToScene(QPoint{1, 1}).x() - rect.left();
		scene()->setViewport(rect, scale);
	}
	QGraphicsView::paintEvent(event);
}
