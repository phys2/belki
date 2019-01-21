#ifndef HEATMAPSCENE_H
#define HEATMAPSCENE_H

#include "dataset.h"

#include <QGraphicsScene>
#include <QAbstractGraphicsShapeItem>

class HeatmapScene : public QGraphicsScene
{
	Q_OBJECT
public:

	struct Profile : public QAbstractGraphicsShapeItem
	{
		Profile(unsigned index, const std::vector<double> &features, QGraphicsItem *parent = nullptr);

		QRectF boundingRect() const override;
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

		unsigned index;
		 // feature vector (own copy), TODO: we could avoid a copy but would need to lock dataset…
		std::vector<double> features;

	protected:
		// override for internal use (does not work through pointer! scene() is non-virtual)
		HeatmapScene* scene() const { return qobject_cast<HeatmapScene*>(QAbstractGraphicsShapeItem::scene()); }

		void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

		bool highlight = false;
	};

	HeatmapScene(Dataset &data);

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void rearrange(QSize viewport);
	void rearrange(unsigned columns);
	void recolor();
	void reorder(); // TODO: make order configurable

protected:
	Dataset &data;

	struct {
		QColor bg = Qt::white, fg = Qt::black;
		QColor cursor = Qt::red;
		bool inverted = true;
		bool mixin = true;

		qreal expansion = 10; // x-scale of items
		qreal margin = 10; // x-margin of items
	} style;

	struct {
		unsigned rows, columns = 1;
		qreal columnWidth = 0.;
	} layout;

	std::vector<Profile*> profiles;
};

#endif // HEATMAPSCENE_H
