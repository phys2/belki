#ifndef FEATWEIGHTSSCENE_H
#define FEATWEIGHTSSCENE_H

#include "../widgets/graphicsscene.h"
#include "dataset.h"
#include "utils.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <opencv2/core/core.hpp>

#include <unordered_set>

#undef ABSOLUTE // defined by wingdi.h
#undef RELATIVE // defined by wingdi.h

class FeatweightsScene : public GraphicsScene
{
	Q_OBJECT
public:
	enum class Weighting {
		UNWEIGHTED,
		ABSOLUTE,
		RELATIVE,
		OFFSET
	};
	Q_ENUM(Weighting)

	class WeightBar : public QGraphicsItem {
	public:
		explicit WeightBar(QGraphicsItem *parent = nullptr);

		QRectF boundingRect() const override { return {0, 0, 1, 1}; }
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

	protected:
		// override for internal use (does not work through pointer! scene() is non-virtual)
		FeatweightsScene* scene() const { return qobject_cast<FeatweightsScene*>(QGraphicsItem::scene()); }

		void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

		int highlight = -1;
	};

	explicit FeatweightsScene(Dataset::Ptr data);

signals:
	void cursorChanged(std::vector<ProteinId> proteins, QString title = {});

public slots:
	void updateMarkers();
	void toggleMarkers(const std::vector<ProteinId> &id, bool present);
	void toggleImage(bool useAlternate);
	void updateColorset(QVector<QColor> colors);
	void setWeighting(Weighting weighting);
	void applyScoreThreshold(double threshold);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

	void setDisplay();
	void computeWeights();
	void computeImage(const Features::Vec &features);
	void computeMarkerContour();

	std::set<unsigned> markers; // markers in dataset index (not protein id!)

	std::vector<std::vector<unsigned>> contours;
	std::vector<double> weights;
	Weighting weighting = Weighting::UNWEIGHTED;
	unsigned imageIndex = 0; // index in images to display

	std::function<QPointF(cv::Point_<unsigned>)> translate = [] (cv::Point_<unsigned>) { return QPointF(); };
	cv::Mat1f matrix;
	std::array<QPixmap, 2> images;
	QGraphicsPixmapItem *display;
	QGraphicsPathItem *markerContour;
	WeightBar *weightBar;

	Dataset::Ptr data;
	Features::Vec clippedFeatures; // score threshold applied
	QVector<QColor> colorset;
};

#endif
