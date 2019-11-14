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
	using Border = Qt::Edge;

	RangeSelectItem(QtCharts::QChart *parent = nullptr);
	virtual QRectF boundingRect() const;
	virtual void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*);
	void setBorder(Border border, double x);

public slots:
	void setLimits(double min, double max);
	void setRange(double min, double max);
	void setSubtle(bool on);

signals:
	void borderChanged(Border border, double value);

protected slots:
	void setRect(const QRectF &area); // internally connected to parent

protected:
	struct HandleItem : QGraphicsRectItem {
		HandleItem(Border border, RangeSelectItem *parent);
		void setStyle(bool subtle);

		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
		QPointF restrictPosition(QPointF newPos);

		Border border;
		RangeSelectItem* parent = nullptr;
		double value, limit;
	};

	qreal valueToPos(double value) const;
	double posToValue(qreal x) const;

	void updatePositions();

	bool subtle = false; // subtle style

	QRectF area;
	std::map<Border, HandleItem*> handles;
};

#endif // SIZEGRIPITEM_H
