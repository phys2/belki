#include "featweightsscene.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <QtDebug>

FeatweightsScene::FeatweightsScene(Dataset &data)
    : data(data)
{
	display = new QGraphicsPixmapItem;
	display->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
	display->setCursor(Qt::CursorShape::CrossCursor);
	addItem(display); // scene takes ownership and will clean it up

	qreal offset = .1; // some "feel good" borders
	setSceneRect({QPointF{-offset, -offset}, QPointF{1. + offset, 1. + offset}});
}

void FeatweightsScene::setViewport(const QRectF &rect, qreal scale)
{
	GraphicsScene::setViewport(rect, scale);

	rearrange();
}

void FeatweightsScene::setDisplay()
{
	//display->setPixmap(matrices[currentDirection].image);

	/* normalize display size on screen and also flip Y-axis */
	auto scale = 1./display->boundingRect().width();
	display->setTransform(QTransform::fromTranslate(0, 1).scale(scale, -scale));
	display->setVisible(true);
}

void FeatweightsScene::reset(bool haveData)
{
	//matrices.clear();
	display->setVisible(false);
	//dimensionLabels.clear();

	if (!haveData) {
		return;
	}

	// setup new dimension labels
	auto dim = data.peek()->dimensions; // QStringList COW
	//for (int i = 0; i < dim.size(); ++i)
	    //dimensionLabels.emplace_back(this, (qreal)(i+0.5)/dim.size(), dim[i]);

	// trigger computation TODO
}

void FeatweightsScene::rearrange()
{
	/* rescale & shift clusterbars */
	QPointF margin{15.*vpScale, 15.*vpScale};
	auto topleft = viewport.topLeft() + margin;
	auto botright = viewport.bottomRight() - margin;
	qreal outerMargin = 10.*vpScale; // 10 pixels
	//clusterbars.rearrange({topleft, botright}, outerMargin);

	/* rescale & shift labels */
	/*for (auto& [i, m] : markers)
		m.rearrange(viewport.left(), vpScale);
	for (auto &l : dimensionLabels)
		l.rearrange(viewport.left(), vpScale);*/
}

void FeatweightsScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (!display->scene())
		return; // nothing displayed right now

	auto pos = display->mapFromScene(event->scenePos());

	/* check if cursor lies over matrix */
	// shrink width/height to avoid index out of bounds later
	auto inside = display->boundingRect().adjusted(0,0,-0.01,-0.01).contains(pos);
	if (!inside) {
		emit cursorChanged({});
	}

	// use floored coordinates, as everything in [0,1[ lies over pixel 0
	cv::Point_<unsigned> idx = {(unsigned)pos.x(), (unsigned)pos.y()};
	// need to back-translate
	auto d = data.peek();
	idx = {d->order.index[(size_t)pos.x()],
	       d->order.index[(size_t)pos.y()]};

	/* display current value */
//	auto &m = matrices[currentDirection].matrix;
//	display->setToolTip(QString::number((double)m(idx), 'f', 2));

	/* emit cursor change */
	emit cursorChanged({idx.x, idx.y});
}

void FeatweightsScene::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
}
