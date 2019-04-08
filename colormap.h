#ifndef COLORMAP_H
#define COLORMAP_H

#include <QPixmap>
#include <opencv2/core/core.hpp>

namespace colormap {

	// convert Mat1b
	QPixmap apply(cv::Mat1b &source);
	// convert any matrix (includes conversion to Mat1b)
	QPixmap apply(cv::Mat &source, double scale);

	// Magma color map
	extern std::array<cv::Vec3b, 256> magma;
};

#endif
