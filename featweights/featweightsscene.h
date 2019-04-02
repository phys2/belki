#ifndef FEATWEIGHTSSCENE_H
#define FEATWEIGHTSSCENE_H

#include "widgets/graphicsscene.h"
#include "dataset.h"
#include "utils.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <map>

class FeatweightsScene : public GraphicsScene
{
	Q_OBJECT
public:
	FeatweightsScene(Dataset &data);

	void setViewport(const QRectF &rect, qreal scale) override;

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);

	void updateColorset(QVector<QColor> colors);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	void setDisplay();
	void rearrange();

	Dataset &data;
	QVector<QColor> colorset;

	QGraphicsPixmapItem *display;
};

#endif
