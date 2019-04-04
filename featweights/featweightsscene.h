#ifndef FEATWEIGHTSSCENE_H
#define FEATWEIGHTSSCENE_H

#include "widgets/graphicsscene.h"
#include "dataset.h"
#include "utils.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <opencv2/core/core.hpp>

class FeatweightsScene : public GraphicsScene
{
	Q_OBJECT
public:

	class WeightBar : public QGraphicsItem {
	public:
		WeightBar(QGraphicsItem *parent = nullptr);

		QRectF boundingRect() const override { return {0, 0, 1, 1}; }
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

	protected:
		// override for internal use (does not work through pointer! scene() is non-virtual)
		FeatweightsScene* scene() const { return qobject_cast<FeatweightsScene*>(QGraphicsItem::scene()); }

		void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

		int highlight = -1;
	};

	FeatweightsScene(Dataset &data);

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void toggleMarker(unsigned sampleIndex, bool present);
	void updateColorset(QVector<QColor> colors);
	void changeWeighting(int weighting);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

	void setDisplay();
	void computeWeights();
	void computeImage();
	void computeMarkerContour();

	Dataset &data;
	QVector<QColor> colorset;

	std::set<unsigned> markers;

	std::vector<std::vector<unsigned>> contours;
	std::vector<double> weights;
	int weighting; // TODO enum

	std::function<QPointF(cv::Point_<unsigned>)> translate = [] (cv::Point_<unsigned>) { return QPointF(); };
	cv::Mat1f matrix;
	QPixmap image;
	QGraphicsPixmapItem *display;
	QGraphicsPathItem *markerContour;
	WeightBar *weightBar;
};

#endif
