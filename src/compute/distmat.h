#ifndef DISTMAT_H
#define DISTMAT_H

#include "features.h"

#include <QPixmap>
#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

namespace distmat
{
	using TranslateFun = std::function<cv::Point(int,int)>;

	cv::Mat1f computeMatrix(const std::vector<std::vector<double>> &features, Distance measure);
	QPixmap computeImage(const cv::Mat1f &matrix, Distance measure);
	QPixmap computeImage(const cv::Mat1f &matrix, Distance measure, const TranslateFun &translate);
}

#endif // DISTMAT_H
