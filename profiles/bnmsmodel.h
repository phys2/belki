#ifndef BNMSMODEL_H
#define BNMSMODEL_H

#include <vector>

/* We are doing a quick hack here to store our own data.
 * If this stuff is deemed valuable, this should go to model.h,
 * and the data loaded by storage.h, stored in dataset.h, etc.
 *
 * and have compute/bnms maybe for all the dist/matching stuff
 */

struct Component {
	double weight;
	double mean;
	double sigma;
	// vector coordinates covered by pdf
	std::pair<size_t, size_t> cover = {0, 0};
};

// components of a protein, sorted by mean (left-to-right)
using Components = std::vector<Component>;

#endif
