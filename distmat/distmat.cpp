#include "distmat.h"
#include "colormap.h"

#include <opencv2/imgproc/imgproc.hpp>
#include <tbb/parallel_for_each.h>

std::map<Distmat::Measure, Distmat::MeasureFun> Distmat::measures()
{
	auto distL2 = [] (const auto &a, const auto &b) {
		return cv::norm(a, b, cv::NORM_L2);
	};
	auto distCrossCorr = [] (const auto &a, const auto &b) {
		double corr1 = 0., corr2 = 0., crosscorr = 0.;
		for (unsigned i = 0; i < a.size(); ++i) {
			auto v1 = a[i], v2 = b[i];
			corr1 += v1*v1;
			corr2 += v2*v2;
			crosscorr += v1*v2;
		}
		return crosscorr / (sqrt(corr1) * sqrt(corr2));
	};
	auto distPearson = [&] (const auto &a, const auto &b) {
		std::vector<double> aa, bb;
		cv::subtract(a, cv::mean(a), aa);
		cv::subtract(b, cv::mean(b), bb);
		return distCrossCorr(aa, bb);
	};

	return {
		{Measure::NORM_L2, distL2},
		{Measure::CROSSCORREL, distCrossCorr},
		{Measure::PEARSON, distPearson},
	};
}

void Distmat::computeMatrix(const std::vector<std::vector<double>> &features)
{
	auto sidelen = features.size();
	matrix = cv::Mat1f(sidelen, sidelen);

	/* amass all the combinations we need for filling a symmetric matrix */
	std::vector<cv::Point_<size_t>> coords;
	for (size_t y = 0; y < (unsigned)sidelen; ++y) {
		for (size_t x = 0; x <= y; ++x)
			coords.push_back({x, y});
	}

	/* get the work done in parallel */
	auto m = measures()[measure];
	tbb::parallel_for((size_t)0, coords.size(), [&] (size_t i) {
		auto c = coords[i];
		const auto &a = features[c.x], &b = features[c.y];
		matrix(c) = matrix(c.x, c.y) = (float)m(a, b);
	});
}

void Distmat::computeImage(const TranslateFun &translate)
{
	/* determine shift & scale to fit into uchar */
	double minVal, maxVal;
	switch (measure) {
	case Measure::PEARSON:
		minVal = -1.;
		maxVal = 1.;
		break;
	case Measure::CROSSCORREL:
		minVal = 0.;
		maxVal = 1.;
		break;
	default:
		cv::minMaxLoc(matrix, &minVal, &maxVal);
	}

	/* convert to Mat1b and reorder at the same time */
	double scale = 255./(maxVal - minVal);
	cv::Mat1b matrixB(matrix.rows, matrix.cols);
	tbb::parallel_for(0, matrix.rows, [&] (int y) {
		for (int x = 0; x <= y; ++x)
			matrixB(y, x) = matrixB(x, y)
			        = (uchar)((matrix(translate(y, x)) - minVal)*scale);
	});

	image = colormap::apply(matrixB);
}
