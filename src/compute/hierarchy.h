#ifndef HIERARCHY_H
#define HIERARCHY_H

#include "model.h"

#include <memory>

namespace hierarchy {

std::unique_ptr<HrClustering> agglomerative(const cv::Mat1f &distances, const std::vector<ProteinId> &proteins);
Annotations partition(const HrClustering &in, unsigned granularity);

}

#endif // HIERARCHY_H
