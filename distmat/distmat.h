#ifndef DISTMAT_H
#define DISTMAT_H

#include <QVector>
#include <QPixmap>

#include <opencv2/core/core.hpp>
#include <functional>
#include <map>

struct Distmat
{
	enum class Measure {
		NORM_L2,
		CROSSCORREL,
		PEARSON
	};

	using MeasureFun = std::function<double(const std::vector<double> &, const std::vector<double> &)>;
	using TranslateFun = std::function<cv::Point(int,int)>;

	static std::map<Measure, MeasureFun> measures();

	void computeMatrix(const std::vector<std::vector<double>> &features);
	void computeImage(const TranslateFun &translate);

	Measure measure = Measure::CROSSCORREL;
	cv::Mat1f matrix;
	QPixmap image;
};

#endif // DISTMAT_H
