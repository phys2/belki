#ifndef GRAPHICSSCENE_H
#define GRAPHICSSCENE_H

#include <QGraphicsScene>

class GraphicsScene : public QGraphicsScene
{
	Q_OBJECT

public:
	virtual void setViewport(const QRectF &rect, qreal scale);

	virtual void hibernate() {}
	virtual void wakeup() {}

protected:

	/* geometry of the current view, used to re-arrange stuff into view */
	QRectF viewport;
	qreal vpScale;
};

#endif // GRAPHICSSCENE_H
