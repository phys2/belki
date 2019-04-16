#ifndef COLORMAP_H
#define COLORMAP_H

#include <QPixmap>
#include <opencv2/core/core.hpp>

struct Colormap {
	Colormap(const std::array<cv::Vec3b, 256>& map) : map(map) {}

	// convert Mat1b
	cv::Mat3b apply(const cv::Mat1b &source);
	// convert any matrix (includes conversion to Mat1b)
	cv::Mat3b apply(const cv::Mat &source, double scale, double minVal=0.);

	const std::array<cv::Vec3b, 256> map;

	// produce QColor from Vec3b (note: assumes RGB, not OpenCV's BGR)
	static QColor qcolor(const cv::Vec3b &color);

	// convert any matrix to Mat1b
	static cv::Mat1b prepare(const cv::Mat &source, double scale, double minVal=0.);

	// convert colormap matrix to pixmap
	static QPixmap pixmap(const cv::Mat3b &source);

	// Magma color map
	static Colormap magma;

	// Red to green color map (for score displays, red == bad, green == good)
	static Colormap stoplight;
};

#endif
