#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "widgets/graphicsscene.h"
#include "dataset.h"
#include "distmat.h"
#include "utils.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <map>
#include <unordered_map>

class DistmatScene : public GraphicsScene
{
	Q_OBJECT
public:
	using Direction = Dataset::Direction;

	struct LegendItem
	{
		LegendItem(DistmatScene* scene, qreal coord, QString label);

		void setVisible(bool visible);
		void rearrange(qreal right, qreal scale);

		qreal coordinate;

	protected:
		LegendItem(qreal coord); // must be followed by a call to setup()
		void setup(DistmatScene *scene, QString label, QColor color);

		// items are added to the scene, so we make them non-copyable
		std::unique_ptr<QGraphicsSimpleTextItem> label;
		std::unique_ptr<QGraphicsLineItem> line;
		std::unique_ptr<QGraphicsRectItem> backdrop;
	};

	struct Marker : public LegendItem {
		Marker(DistmatScene* scene, unsigned sampleIndex, ProteinId id);

		unsigned sampleIndex;
	};

	class Clusterbars : NonCopyable // adds its items to the scene
	{
	public:
		Clusterbars(DistmatScene *scene);

		void update(QImage content);
		void setVisible(bool visible);
		void rearrange(QRectF target, qreal margin);

	protected:
		bool valid = false; // do items show valid content
		std::map<Qt::Edge, QGraphicsPixmapItem*> items;
	};

	DistmatScene(Dataset &data, bool dialogMode = false);

	void setViewport(const QRectF &rect, qreal scale) override;

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void selectionChanged(const std::vector<bool> dimensionSelected);

public slots:
	void reset(bool haveData = false);
	void setDirection(Direction direction);
	void reorder();
	void recolor();

	void updateColorset(QVector<QColor> colors);

	void toggleMarker(ProteinId id, bool present);
	void togglePartitions(bool showPartitions);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

	void setDisplay();
	void rearrange();
	void updateVisibilities();
	void updateRenderQuality();
	qreal computeCoord(unsigned sampleIndex);

	Direction currentDirection = Direction::PER_PROTEIN;
	std::map<Direction, Distmat> matrices;

	Dataset &data;
	QVector<QColor> colorset;

	QGraphicsPixmapItem *display;

	// we are used in a dialog
	bool dialogMode;
	// annotations used in PER_PROTEIN:
	bool showPartitions = true;
	Clusterbars clusterbars;
	std::unordered_map<ProteinId, Marker> markers;
	// annotations used in PER_DIRECTION:
	std::map<unsigned, LegendItem> dimensionLabels;

	// dialog mode: selectable dimensions
	std::vector<bool> dimensionSelected;
};

#endif // DISTMATSCENE_H
