#include "distmat.h"
#include "../compute/colors.h"

#include <tbb/parallel_for.h>

void Distmat::computeMatrix(const std::vector<std::vector<double>> &features)
{
	auto sidelen = features.size();
	matrix = cv::Mat1f(sidelen, sidelen);

	/* amass all the combinations we need for filling a symmetric matrix */
	std::vector<cv::Point2i> coords; // use int for matrix() indexing later on
	for (int y = 0; y < (int)sidelen; ++y) {
		for (int x = 0; x <= y; ++x)
			coords.push_back({x, y});
	}

	/* get the work done in parallel */
	auto dist = features::distfun(measure);
	tbb::parallel_for((size_t)0, coords.size(), [&] (size_t i) {
		auto c = coords[i];
		const auto &a = features[(size_t)c.x], &b = features[(size_t)c.y];
		matrix(c) = matrix(c.x, c.y) = (float)dist(a, b);
	});
}

void Distmat::computeImage(const TranslateFun &translate)
{
	/* determine shift & scale to fit into uchar */
	double minVal, maxVal;
	switch (measure) {
	case features::Distance::PEARSON:
		minVal = -1.;
		maxVal = 1.;
		break;
	case features::Distance::CROSSCORREL:
		minVal = 0.;
		maxVal = 1.;
		break;
	default:
		cv::minMaxLoc(matrix, &minVal, &maxVal);
	}

	/* convert to Mat1b and reorder at the same time */
	float scale = 255./(maxVal - minVal);
	cv::Mat1b matrixB(matrix.rows, matrix.cols);
	tbb::parallel_for(0, matrix.rows, [&] (int y) {
		for (int x = 0; x <= y; ++x)
			matrixB(y, x) = matrixB(x, y)
			        = (uchar)((matrix(translate(y, x)) - (float)minVal)*scale);
	});

	image = Colormap::pixmap(Colormap::magma.apply(matrixB));
}
