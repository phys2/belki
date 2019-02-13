#include "heatmapview.h"
#include "heatmapscene.h"

#include <QKeyEvent>

#include <QtDebug>

HeatmapScene *HeatmapView::scene() const
{
	return qobject_cast<HeatmapScene*>(QGraphicsView::scene());
}

void HeatmapView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	// TODO: needed in Heatmap view?
	setFocus(Qt::MouseFocusReason);
}

void HeatmapView::keyReleaseEvent(QKeyEvent *event)
{
	QGraphicsView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;

	// TODO make toolbar button
	if (event->key() == Qt::Key_S) {
		singleColumn = !singleColumn;
		arrangeScene();
	}
}

void HeatmapView::wheelEvent(QWheelEvent *event)
{
	if (singleColumn && !(event->modifiers() & Qt::ControlModifier)) {
		QGraphicsView::wheelEvent(event);
		return;
	}

	auto anchor = transformationAnchor();
	setTransformationAnchor(AnchorUnderMouse);
	auto angle = event->angleDelta().y();
	qreal factor = std::pow(1.2, angle / 240.0);
	scale(factor, factor);

	setTransformationAnchor(anchor);
}

void HeatmapView::resizeEvent(QResizeEvent *event)
{
	arrangeScene();

	QGraphicsView::resizeEvent(event);
}

void HeatmapView::paintEvent(QPaintEvent *event)
{
	auto scale = mapToScene(QRect(0, 0, 1, 1)).boundingRect().width();
	if (scale != currentScale) {
		currentScale = scale;
		if (currentScale > outerScale && !singleColumn) {
			arrangeScene();
			currentScale = outerScale;
		}
		scene()->setScale(currentScale);
	}

	QGraphicsView::paintEvent(event);
}

void HeatmapView::arrangeScene()
{
	if (!scene())
		return;

	if (singleColumn) {
		scene()->rearrange(1);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		resetTransform();
		centerOn(sceneRect().center());
	} else {
		scene()->rearrange(contentsRect().size());
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		fitInView(scene()->sceneRect(), Qt::AspectRatioMode::KeepAspectRatio);
	}
	outerScale = mapToScene(QRect(0, 0, 1, 1)).boundingRect().width();
}
