#ifndef HEATMAPSCENE_H
#define HEATMAPSCENE_H

#include "dataset.h"
#include "utils.h"

#include <QGraphicsScene>
#include <QAbstractGraphicsShapeItem>

#include <opencv2/core.hpp>
#include <unordered_map>
#include <memory>

class WindowState;

class HeatmapScene : public QGraphicsScene
{
	Q_OBJECT
public:

	struct Profile : public QAbstractGraphicsShapeItem
	{
		Profile(unsigned index, View<Dataset::Base> &data);

		QRectF boundingRect() const override;
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

		unsigned index;
		// feature vector as alpha values (0…255)
		cv::Mat1b features;
		// scores as color values (RGB)
		cv::Mat3b scores;

	protected:
		// override for internal use (does not work through pointer! scene() is non-virtual)
		HeatmapScene* scene() const { return qobject_cast<HeatmapScene*>(QAbstractGraphicsShapeItem::scene()); }

		void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
		void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;

		bool highlight = false;
	};

	struct Marker
	{
		Marker(HeatmapScene* scene, unsigned sampleIndex, const QPointF &pos);

		void rearrange(const QPointF &pos);

		unsigned sampleIndex;

	protected:
		HeatmapScene* scene() const { return qobject_cast<HeatmapScene*>(label->scene()); }

		// items are added to the scene, so we make them non-copyable
		std::unique_ptr<QGraphicsSimpleTextItem> label;
		std::unique_ptr<QGraphicsLineItem> line;
		std::unique_ptr<QGraphicsRectItem> backdrop;
	};

	HeatmapScene(Dataset::Ptr data);

	void setState(std::shared_ptr<WindowState> s);
	void setScale(qreal scale);

	void hibernate();
	void wakeup();

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void rearrange(QSize viewport);
	void rearrange(unsigned columns);
	void recolor();
	void reorder();

	void updateMarkers();
	void toggleMarkers(const std::vector<ProteinId> &ids, bool present);
	void updateAnnotations();

protected:
	bool awake = false;

	struct {
		QColor bg = Qt::white, fg = Qt::black;
		QColor cursor = Qt::blue;
		bool inverted = true;
		bool mixin = true;

		qreal expansion = 10; // x-scale of items
		qreal margin = 10; // x-margin of items
	} style;

	struct {
		unsigned rows = 0, columns = 1;
		qreal columnWidth = 0.;
	} layout;

	std::vector<Profile*> profiles;
	std::unordered_map<ProteinId, Marker> markers;

	QSize viewport; // size of the viewport in _screen_ coordinates
	qreal pixelScale; // size of a pixel in scene coordinates

	Dataset::Ptr data;
	std::shared_ptr<WindowState> state;
};

#endif // HEATMAPSCENE_H
