#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"

#include <QGraphicsScene>
#include <QGraphicsItemGroup>

#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QGraphicsLineItem;

class DistmatScene : public QGraphicsScene
{
	Q_OBJECT
public:
	enum class Measure {
		NORM_L2,
		CROSSCORREL,
		PEARSON
	};

	struct Marker : QGraphicsItemGroup {
		Marker(unsigned sampleIndex, qreal coordY, DistmatScene* scene);
		// no copies/moves! adds itself to the scene in above constructor
		Marker(const Marker&) = delete;
		Marker& operator=(const Marker&) = delete;
		~Marker() { delete label; delete line; }
		unsigned sampleIndex;
		QGraphicsSimpleTextItem *label;
		QGraphicsLineItem *line;
	};

	DistmatScene(Dataset &data);
	static std::map<Measure, std::function<double(const std::vector<double> &,
	                                     const std::vector<double> &)>> measures();

	void setViewport(const QRectF &rect, qreal scale);

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void reorder();
	void recolor();

	void addMarker(unsigned sampleIndex);
	void removeMarker(unsigned sampleIndex);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	void rearrange();

	Dataset &data;

	Measure measure = Measure::CROSSCORREL;
	cv::Mat1f distmat;
	cv::Mat3b distimg;
	QGraphicsPixmapItem *display;
	std::map<Qt::Edge, QGraphicsPixmapItem*> clusterbars;
	std::map<unsigned, Marker*> markers;

	/* geometry of the current view, used to re-arrange stuff into view */
	QRectF viewport;
	qreal vpScale;

};

#endif // DISTMATSCENE_H
