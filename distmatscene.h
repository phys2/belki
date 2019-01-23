#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"

#include <QGraphicsScene>

#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;

class DistmatScene : public QGraphicsScene
{
	Q_OBJECT
public:
	enum class Measure {
		NORM_L2,
		CROSSCORREL,
		PEARSON
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
	std::map<unsigned, QGraphicsSimpleTextItem*> markers;

	/* geometry of the current view, used to re-arrange stuff into view */
	QRectF viewport;
	qreal vpScale;

};

#endif // DISTMATSCENE_H
