#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"

#include <QGraphicsScene>
#include <QAbstractGraphicsShapeItem>

#include <opencv2/core/core.hpp>

class DistmatScene : public QGraphicsScene
{
	Q_OBJECT
public:
	DistmatScene(Dataset &data);

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void reorder();

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	Dataset &data;

	cv::Mat1f distmat;
	cv::Mat3b distimg;
	QGraphicsPixmapItem *display;
};

#endif // DISTMATSCENE_H
