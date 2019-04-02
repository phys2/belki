#include "graphicsscene.h"

GraphicsScene::GraphicsScene()
{

}

void GraphicsScene::setViewport(const QRectF &rect, qreal scale)
{
	viewport = rect;
	vpScale = scale;
}
