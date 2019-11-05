#ifndef RANGESELECTITEM_H
#define RANGESELECTITEM_H

#include <QGraphicsObject>
#include <map>

namespace QtCharts {
class QChart;
}

class RangeSelectItem : public QGraphicsObject
{
	Q_OBJECT

public:
	enum Border {
		LEFT, RIGHT
	};

	RangeSelectItem(QtCharts::QChart *parent = nullptr);
	virtual QRectF boundingRect() const;
	virtual void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) {}
	void setBorder(Border border, qreal x);

public slots:
	void setRect(const QRectF &area);
	void setLimits(qreal min, qreal max);
	void setRange(qreal min, qreal max);

signals:
	void borderChanged(Border border, qreal value);

private:
	struct HandleItem : QGraphicsRectItem {
		HandleItem(Border border, RangeSelectItem *parent);

		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
		QPointF restrictPosition(QPointF newPos);

		Border border;
		RangeSelectItem* parent = nullptr;
		qreal value, limit;
	};

	qreal valueToPos(qreal value) const;
	qreal posToValue(qreal x) const;

	void updatePositions();

	QRectF area;
	std::map<Border, HandleItem*> handles;
};

#endif // SIZEGRIPITEM_H
