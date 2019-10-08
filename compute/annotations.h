#ifndef ANNOTATIONS_H
#define ANNOTATIONS_H

#include "model.h"
#include <memory>
#include <mutex>

namespace seg_meanshift {
class FAMS;
}

namespace annotations {

void prune(Annotations &data); // remove small clusters
void order(Annotations &data, bool genericNames);
void color(Annotations &data, const QVector<QColor> &colors);
Annotations partition(const HrClustering &in, unsigned granularity);

class Meanshift {

public:
	struct Result {
		Features::Vec modes;
		std::vector<int> associations;
	};

	explicit Meanshift(const Features::Vec &input);
	~Meanshift();

	std::optional<Result> applyK(float k);
	void cancel();

protected:
	std::optional<Result> compute();

	float k = 0; // the currently desired k, 0 means cancel
	std::unique_ptr<seg_meanshift::FAMS> fams;
	std::mutex l;
};

}

#endif
