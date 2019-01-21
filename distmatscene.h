#ifndef DISTMATSCENE_H
#define DISTMATSCENE_H

#include "dataset.h"

#include <QGraphicsScene>
#include <QAbstractGraphicsShapeItem>

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

	DistmatScene(Dataset &data);
	static std::map<Measure, std::function<double(const std::vector<double> &,
	                                     const std::vector<double> &)>> measures();

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});

public slots:
	void reset(bool haveData = false);
	void reorder();

protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

	Dataset &data;

	Measure measure = Measure::CROSSCORREL;
	cv::Mat1f distmat;
	cv::Mat3b distimg;
	QGraphicsPixmapItem *display;
};

#endif // DISTMATSCENE_H
