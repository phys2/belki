#include "rangeselectitem.h"

#include <QtCharts/QChart>
#include <QBrush>
#include <QPen>
#include <QCursor>
#include <QPainter>

const auto LEFT = Qt::Edge::LeftEdge;
const auto RIGHT = Qt::Edge::RightEdge;

RangeSelectItem::RangeSelectItem(QtCharts::QChart *parent)
    : QGraphicsObject(parent)
{
	setZValue(10);

	handles[LEFT] = new HandleItem(LEFT, this);
	handles[RIGHT] = new HandleItem(RIGHT, this);
	updatePositions();

	connect(parent, &QtCharts::QChart::plotAreaChanged,
	        this, &RangeSelectItem::setRect);
}

QRectF RangeSelectItem::boundingRect() const
{
	return area;
}

void RangeSelectItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
	if (subtle)
		return;

	QBrush fill({255, 195, 195, 127});
	fill.setStyle(Qt::BrushStyle::Dense4Pattern);
	std::array<QRectF, 2> areas = {
	    QRectF{area.topLeft(), QPointF{valueToPos(handles[LEFT]->value), area.bottom()}},
	    {QPointF{valueToPos(handles[RIGHT]->value), area.top()}, area.bottomRight()}
	};
	for (auto &a : areas)
		painter->fillRect(a, fill);
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

void RangeSelectItem::setLimits(double min, double max)
{
	handles.at(LEFT)->limit = min;
	handles.at(RIGHT)->limit = max;
}

void RangeSelectItem::setRange(double min, double max)
{
	setBorder(LEFT, min);
	setBorder(RIGHT, max);
}

void RangeSelectItem::setSubtle(bool on)
{
	if (subtle == on)
		return;

	subtle = on;
	for (auto &[_, item] : handles)
		item->setStyle(subtle);
	update();
}

void RangeSelectItem::setBorder(Border border, double x)
{
	handles.at(border)->value = x;
	updatePositions();
	emit borderChanged(border, x);
}

qreal RangeSelectItem::valueToPos(double value) const
{
	return (value - handles.at(LEFT)->limit)
	        / (handles.at(RIGHT)->limit - handles.at(LEFT)->limit)
	        * area.width() + area.left();
}

double RangeSelectItem::posToValue(qreal x) const
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
	update();
}


RangeSelectItem::HandleItem::HandleItem(Border border, RangeSelectItem* parent)
    : QGraphicsRectItem((border == LEFT ? -15 : 0), 0, 15, 10, parent),
      border(border),
      parent(parent)
{
	setPen(Qt::NoPen);
	setFlag(ItemIsMovable);
	setFlag(ItemSendsGeometryChanges);
	setFlag(ItemIgnoresTransformations);
	setCursor(Qt::SizeHorCursor);
	setStyle(parent->subtle);
}

void RangeSelectItem::HandleItem::setStyle(bool subtle)
{
	QLinearGradient grad((border == LEFT ? -15 : 0), 0, (border == LEFT ? 0 : 15), 0);
	if (subtle) {
		grad.setColorAt((border == LEFT ? 0 : 1), {255, 255, 255, 0});
		grad.setColorAt((border == LEFT ? 1 : 0), {0, 0, 255, 127});
	} else {
		grad.setColorAt((border == LEFT ? 0 : 1), {255, 255, 255, 0});
		grad.setColorAt((border == LEFT ? 1 : 0), {255, 0, 0, 127});
	}
	setBrush(grad);
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
