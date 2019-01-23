#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>

#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

class DistmatScene : public QGraphicsScene
{
	Q_OBJECT
public:
	enum class Measure {
		NORM_L2,
		CROSSCORREL,
		PEARSON
	};

	struct Marker {
		Marker(unsigned sampleIndex, qreal coord, DistmatScene* scene);
		// no copies/moves! adds itself to the scene in above constructor
		Marker(const Marker&) = delete;
		Marker& operator=(const Marker&) = delete;
		~Marker() { delete label; delete line; delete backdrop; }

		void rearrange(qreal right, qreal scale);

		unsigned sampleIndex;
		qreal coordinate;
		QGraphicsSimpleTextItem *label;
		QGraphicsLineItem *line;
		QGraphicsRectItem *backdrop;
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

	void updateColorset(QVector<QColor> colors);

	void addMarker(unsigned sampleIndex);
	void removeMarker(unsigned sampleIndex);

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	void rearrange();

	Dataset &data;
	QVector<QColor> colorset;

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
