#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"
#include "distmat.h"

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

	class LegendItem : QObject {
	public:
		LegendItem(DistmatScene* scene, qreal coord, QString label);
		// no copies/moves! adds its items to the scene in above constructor
		LegendItem(const LegendItem&) = delete;
		LegendItem& operator=(const LegendItem&) = delete;
		~LegendItem() { delete label; delete line; delete backdrop; }

		void setVisible(bool visible);
		void rearrange(qreal right, qreal scale);

		qreal coordinate;

	protected:
		LegendItem(qreal coord); // must be followed by a call to setup()
		void setup(DistmatScene *scene, QString label, QColor color);

		QGraphicsSimpleTextItem *label;
		QGraphicsLineItem *line;
		QGraphicsRectItem *backdrop;
	};

	struct Marker : public LegendItem {
		Marker(DistmatScene* scene, unsigned sampleIndex, qreal coord);

		unsigned sampleIndex;
	};

	class Clusterbars : QObject {
	public:
		Clusterbars(DistmatScene *scene);
		// no copies/moves! adds its items to the scene in above constructor
		Clusterbars(const Clusterbars&) = delete;
		Clusterbars& operator=(const Clusterbars&) = delete;

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

	Direction currentDirection = Direction::PER_PROTEIN;
	std::map<Direction, Distmat> matrices;

	Dataset &data;
	QVector<QColor> colorset;

	QGraphicsPixmapItem *display;

	// annotations used in PER_PROTEIN:
	Clusterbars clusterbars;
	std::map<unsigned, Marker*> markers;
	// annotations used in PER_DIRECTION:
	std::vector<LegendItem*> dimensionLabels;

	/* geometry of the current view, used to re-arrange stuff into view */
	QRectF viewport;
	qreal vpScale;

};

#endif // DISTMATSCENE_H
