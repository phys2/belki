#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>
#include "fams.h"

using namespace std;

namespace seg_meanshift {

bool FAMS::importPoints(const QVector<std::vector<double>> &features, bool normalize) {
	// w_ and h_ are only used for result output (i.e. in io.cpp)
	n_ = features.size();
	d_ = features.at(0).size(); // dimensionality

	minVal_ = 0;
	maxVal_ = 1;

	// convert to internal unsigned short representation
	dataholder.resize(n_);
	for (unsigned i = 0; i < n_; ++i) {
		auto &source = features[i];
		auto &target = dataholder[i];
		target.resize(d_);

		float factor = 65535.;
		if (normalize) {
			double n = cv::norm(source, cv::NORM_L2);
			if (n == 0.)
				n = 1.;
			// if (n < 1.)
			//	std::cerr << i << "\t" << n << std::endl;
			factor /= n;
		}

		for (unsigned j = 0; j < d_; ++j)
			target[j] = (unsigned short)(source[j] * factor);
	}

	// link points to their data
	datapoints.resize(dataholder.size());
	for (size_t i = 0; i < dataholder.size(); ++i) {
		datapoints[i].data = &dataholder[i];
	}
	return true;
}

std::vector<std::vector<double>> FAMS::exportModes() const {
	std::vector<std::vector<double>> ret;
	for (const auto& src : prunedModes) {
		std::vector<double> dest(d_);
		for (size_t d = 0; d < src.size(); ++d)
			dest[d] = ushort2value(src[d]);
		ret.push_back(std::move(dest));
	}
	return ret;
}

void FAMS::saveModes(const std::string& filename, bool pruned) {

	size_t n = (pruned ? prunedModes.size() : modes.size());
	if (n < 1)
		return;

	FILE* fd = fopen((filename).c_str(), "wb");

	for (size_t i = 0; i < n; ++i) {
		std::vector<unsigned short> &src
				= (pruned ? prunedModes[i] : modes[i].data);
		for (size_t d = 0; d < src.size(); ++d) {
			fprintf(fd, "%g ", ushort2value(src[d]));
		}
		fprintf(fd, "\n");
	}

	fclose(fd);
}

}
