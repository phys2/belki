#include "rangeselectitem.h"

#include <QtCharts/QChart>
#include <QBrush>
#include <QPen>
#include <QCursor>

RangeSelectItem::RangeSelectItem(QtCharts::QChart *parent)
    : QGraphicsObject(parent)
{
	setZValue(10);

	handles[LEFT] = new HandleItem(LEFT, this);
	handles[RIGHT] = new HandleItem(RIGHT, this);
	updatePositions();
}

QRectF RangeSelectItem::boundingRect() const
{
	return {handles.at(LEFT)->rect().topLeft(), handles.at(RIGHT)->rect().bottomRight()};
}

void RangeSelectItem::setRect(const QRectF &newArea)
{
	area = newArea;
	for (auto &[_, item] : handles) {
		item->setFlag(ItemSendsGeometryChanges, false);
		item->setPos(item->pos().x(), area.top());
		item->setRect({item->rect().topLeft(), QSizeF{item->rect().width(), area.height()}});
		item->setFlag(ItemSendsGeometryChanges, true);
	}
	updatePositions();
}

void RangeSelectItem::setLimits(qreal min, qreal max)
{
	handles.at(LEFT)->limit = min;
	handles.at(RIGHT)->limit = max;
}

void RangeSelectItem::setRange(qreal min, qreal max)
{
	setBorder(LEFT, min);
	setBorder(RIGHT, max);
}

void RangeSelectItem::setBorder(Border border, qreal x)
{
	handles.at(border)->value = x;
	updatePositions();
	emit borderChanged(border, x);
}

qreal RangeSelectItem::valueToPos(qreal value) const
{
	return (value - handles.at(LEFT)->limit)
	        / (handles.at(RIGHT)->limit - handles.at(LEFT)->limit)
	        * area.width() + area.left();
}

qreal RangeSelectItem::posToValue(qreal x) const
{
	return (x - area.left())
	        / area.width()
	        * (handles.at(RIGHT)->limit - handles.at(LEFT)->limit) + handles.at(LEFT)->limit;
}

void RangeSelectItem::updatePositions()
{
	for (auto &[_, item] : handles) {
		item->setFlag(ItemSendsGeometryChanges, false);
		item->setPos(valueToPos(item->value), item->pos().y());
		item->setFlag(ItemSendsGeometryChanges, true);
	}
}


RangeSelectItem::HandleItem::HandleItem(Border border, RangeSelectItem* parent)
    : QGraphicsRectItem(-5, 0, 10, 10, parent),
      border(border),
      parent(parent)
{
	setPen(Qt::NoPen);
	setBrush(QColor(255, 0, 0, 127));
	setFlag(ItemIsMovable);
	setFlag(ItemSendsGeometryChanges);
	setFlag(ItemIgnoresTransformations);
	setCursor(Qt::SizeHorCursor);
}

QVariant RangeSelectItem::HandleItem::itemChange(GraphicsItemChange change,
                                              const QVariant &value)
{
	if (change == ItemPositionChange) {
		return restrictPosition(value.toPointF());
	}
	if (change == ItemPositionHasChanged) {
		QPointF pos = value.toPointF();
		parent->setBorder(border, parent->posToValue(pos.x()));
	}
	return value;
}

QPointF RangeSelectItem::HandleItem::restrictPosition(QPointF newPos)
{
	/* We could also do this in screen space with area,
	 * but this way, the enforced offset is in value space */
	auto left = parent->handles[LEFT]->limit;
	auto right = parent->handles[RIGHT]->limit;
	if (border == LEFT)
		right = parent->handles[RIGHT]->value - 10;
	else
		left = parent->handles[LEFT]->value + 10;
	auto newX = std::max(left, std::min(right, parent->posToValue(newPos.x())));
	newPos.setX(parent->valueToPos(newX));
	newPos.setY(pos().y()); // don't allow vertical movement
	return newPos;
}
