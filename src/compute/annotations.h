#ifndef ANNOTATIONS_H
#define ANNOTATIONS_H

#include "model.h"
#include <memory>
#include <mutex>

namespace seg_meanshift {
class FAMS;
}

namespace annotations {

bool equal(const Annotations::Meta &a, const Annotations::Meta &b);

void prune(Annotations &data); // remove small clusters
void order(Annotations &data, bool genericNames);
void color(Annotations &data, const QVector<QColor> &colors);

class Meanshift {

public:
	struct Result {
		Features::Vec modes;
		std::vector<int> associations;
	};

	explicit Meanshift(const Features::Vec &input);
	~Meanshift();

	std::optional<Result> run(float k);
	void cancel();

protected:
	std::unique_ptr<seg_meanshift::FAMS> fams;
	std::mutex l;
};

}

#endif
