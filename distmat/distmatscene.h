#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"
#include "distmat.h"
#include "utils.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <map>

class DistmatScene : public QGraphicsScene
{
	Q_OBJECT
public:
	enum class Direction {
		PER_PROTEIN,
		PER_DIMENSION,
	};
	Q_ENUM(Direction)

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
		Marker(DistmatScene* scene, unsigned sampleIndex);

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
		std::map<Qt::Edge, QGraphicsPixmapItem*> items;
	};

	DistmatScene(Dataset &data);

	void setViewport(const QRectF &rect, qreal scale);

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void setDirection(DistmatScene::Direction direction);
	void reorder();
	void recolor();

	void updateColorset(QVector<QColor> colors);

	void addMarker(unsigned sampleIndex);
	void removeMarker(unsigned sampleIndex);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	void setDisplay();
	void rearrange();
	qreal computeCoord(unsigned sampleIndex);

	Direction currentDirection = Direction::PER_PROTEIN;
	std::map<Direction, Distmat> matrices;

	Dataset &data;
	QVector<QColor> colorset;

	QGraphicsPixmapItem *display;

	// annotations used in PER_PROTEIN:
	Clusterbars clusterbars;
	std::map<unsigned, Marker> markers;
	// annotations used in PER_DIRECTION:
	std::vector<LegendItem> dimensionLabels;

	/* geometry of the current view, used to re-arrange stuff into view */
	QRectF viewport;
	qreal vpScale;
};

#endif // DISTMATSCENE_H
