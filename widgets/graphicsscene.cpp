#include "graphicsscene.h"

void GraphicsScene::setViewport(const QRectF &rect, qreal scale)
{
	viewport = rect;
	vpScale = scale;
}
