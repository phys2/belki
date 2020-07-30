#include "hierarchy.h"
#include "jobregistry.h"

#include <queue>
#include <unordered_set>

namespace hierarchy
{

struct Pair {
	float distance;
	unsigned left, right;
	static float closer(const Pair& a, const Pair& b) { return a.distance > b.distance; }
};

std::unique_ptr<HrClustering> agglomerative(const cv::Mat1f &distances, const std::vector<ProteinId> &proteins) {
	if (distances.rows != distances.cols || distances.rows != (int)proteins.size())
		throw std::invalid_argument("Non-square matrix or unmatching protein vector.");

	auto jr = JobRegistry::get();
	if (jr->isCurrentJobCancelled())
		return {};

	auto ret = std::make_unique<HrClustering>();
	auto &clusters = ret->clusters;
	auto total = proteins.size()*2 - 1;

	/* build initial set of clusters */
	std::vector<std::vector<unsigned>> members(total); // cache for all children of a cluster
	clusters.resize(total);
	for (unsigned i = 0; i < proteins.size(); ++i) {
		auto &cl = clusters[i];
		cl.protein = proteins[i];
		members[i] = {i};
	}

	auto avg_dist = [&](unsigned a, unsigned b) {
		// average linkage (WPGMA)
		float ret = 0.f;
		for (auto i : members[a]) {
			for (auto j : members[b]) {
				ret += distances(i, j);
			}
		}
		return ret / (members[a].size() * members[b].size());
	};

	/* build initial heap of possible pairs for merge */
	std::priority_queue<Pair, std::vector<Pair>, decltype(&Pair::closer)> pairs(Pair::closer);
	for (unsigned i = 0; i < proteins.size(); ++i) {
		if ((i % (proteins.size() / 10)) == 0) {
			if (jr->isCurrentJobCancelled())
				return {};
			jr->setCurrentJobProgress(10. * i / proteins.size());
		}
		for (unsigned j = 0; j < i; ++j) {
			pairs.push({avg_dist(i, j), i, j});
		}
	}

	/* create whole hierarchy starting from initial set */
	for (unsigned i = proteins.size(); i < total; ++i) {
		// note: the progress update mechanic is a bit unsatisfactory, as the last 1% takes longest
		if ((i % (total / 200)) == 0) {
			if (jr->isCurrentJobCancelled())
				return {};
			jr->setCurrentJobProgress(10. + 90. * (i - proteins.size()) / (total - proteins.size()));
		}

		// find viable pair, remove outdated pairs (whose clusters were merged)
		Pair candidate;
		while (true) {
			candidate = pairs.top();
			pairs.pop();
			if (!members[candidate.left].empty() && !members[candidate.right].empty())
				break; // we found a pair that is still valid
		}

		// create new cluster and double-link
		auto &target = clusters[i];
		target.children = {candidate.left, candidate.right};
		for (auto c : target.children)
			clusters[c].parent = i;
		target.distance = candidate.distance;

		// update members - move all members to new cluster
		auto &joint = members[i], &left = members[candidate.left], &right = members[candidate.right];
		joint.swap(left); // clears out left
		joint.insert(joint.end(), right.begin(), right.end());
		right.clear();

		// add new candidate pairs (note we tried TBB but it did not increase performance)
		for (unsigned j = 0; j < i; ++j) {
			if (members[j].empty())
				continue;
			pairs.push({avg_dist(i, j), i, j});
		}
	}

	return ret;
}

Annotations partition(const HrClustering &in, unsigned granularity)
{
	auto &hrclusters = in.clusters;

	granularity = std::min(granularity, (unsigned)hrclusters.size());
	unsigned lowBound = hrclusters.size() - granularity - 1;

	/* determine clusters to be displayed */
	std::unordered_set<unsigned> candidates;
	// we use the fact that input is sorted by distance, ascending
	for (auto i = lowBound; i < hrclusters.size(); ++i) {
		auto &current = hrclusters[i];

		// add either parent or childs, if any of them is eligible by-itself
		auto useChildrenInstead =
		        std::any_of(current.children.begin(), current.children.end(),
		                    [lowBound] (auto c) { return c >= lowBound; });
		if (useChildrenInstead) {
			for (auto c : current.children) {
				if (c < lowBound) // only add what's not covered by granularity
					candidates.insert(c);
			}
		} else {
			candidates.insert(i);
		}
	}

	Annotations ret;

	// helper to recursively assign all proteins to clusters
	std::function<void(unsigned, unsigned)> flood;
	flood = [&] (unsigned hIndex, unsigned cIndex) {
		auto &current = hrclusters[hIndex];
		if (current.protein)
			// would use std::optional::value(), but not available on MacOS 10.13
			ret.groups.at(cIndex).members.push_back(current.protein.value_or(0));
		for (auto c : current.children)
			flood(c, cIndex);
	};

	ret.groups.reserve(candidates.size());
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(hrclusters.size() - i);
		// use index in hierarchy as cluster index as well
		ret.groups[i].name = name; // initializes
		flood(i, i);
	}

	auto name = QString("%2 at granularity %1").arg(granularity).arg(in.meta.name);
	ret.meta = {Annotations::Meta::HIERCUT, 0, name};
	ret.meta.dataset = in.meta.dataset;
	ret.meta.hierarchy = in.meta.id;
	ret.meta.granularity = granularity;
	return ret;
}

}
