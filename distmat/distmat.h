#ifndef DISTMAT_H
#define DISTMAT_H

#include "compute/features.h"

#include <QVector>
#include <QPixmap>

#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

struct Distmat
{
	using TranslateFun = std::function<cv::Point(int,int)>;

	void computeMatrix(const std::vector<std::vector<double>> &features);
	void computeImage(const TranslateFun &translate);

	features::Distance measure = features::Distance::CROSSCORREL;
	cv::Mat1f matrix;
	QPixmap image;
};

#endif // DISTMAT_H
