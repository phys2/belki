#include "dimred.h"
#include <tapkee/tapkee.hpp> // includes Eigen
#include <tapkee/callbacks/precomputed_callbacks.hpp>
#include <tapkee/utils/logging.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp> // for EMD
#include <tbb/tbb.h>

#include <map>

#include <QDebug>

using namespace tapkee;

namespace dimred {

std::vector<dimred::Method> availableMethods()
{
	return {
		{"PCA", "PCA 12", "Principal Component Analysis"},
		{"kPCA EMD", "kPCA EMD 12", "Kernel-PCA, EMD"},
		{"tSNE", "tSNE", "t-distributed stochastic neighbor embedding, L2"},
		{"tSNE EMD", "tSNE EMD", "t-distributed stochastic neighbor embedding, EMD"},
	};
}

QMap<QString, QVector<QPointF>> compute(QString m, QVector<QVector<double> > &features)
{
	qDebug() << "Computing" << m;

	// setup some logging
	tapkee::LoggingSingleton::instance().enable_info();
	tapkee::LoggingSingleton::instance().enable_benchmark();

	// perform dimensionality reduction
	ParametersSet p;
	if (m.startsWith("PCA") || m.startsWith("kPCA")) {
		p, method=(m == "PCA" ? PCA : KernelPCA), target_dimension=3;
	}
	if (m.startsWith("tSNE")) {
		p, method=tDistributedStochasticNeighborEmbedding, target_dimension=2;
	}
	auto parametrized = initialize().withParameters(p);
	auto nFeat = features.size();

	auto precomputeDistances = [&] (bool kernel = false) {
		std::vector<IndexType> indices((unsigned)nFeat);
		DenseMatrix distances(nFeat, nFeat);

		tbb::parallel_for(int(0), nFeat, [&] (int i) {
			qDebug() << "computing distances for " << i;
			indices[(unsigned)i] = i;

			// fill diagonal
			distances(i, i) = 0;

			for (int j = i + 1; j < nFeat; ++j) {
				cv::Mat1f mi(features[i].size(), 1 + 1, 1.f); // weight + value
				cv::Mat1f mj(features[i].size(), 1 + 1, 1.f); // weight + value
				std::copy(features[i].constBegin(), features[i].constEnd(), mi.begin() + 1);
				std::copy(features[j].constBegin(), features[j].constEnd(), mj.begin() + 1);
				// use L1 here as we have scalar inputs anyway
				auto dist = cv::EMD(mi, mj, cv::DIST_L1);

				// fill symmetrically
				distances(j, i) = distances(i, j) = dist;
			}
		});
		if (kernel) {
			auto imean = -1. / distances.mean();
			distances = (distances.array() * imean).exp();
		}
		return std::make_pair(indices, distances);
	};

	TapkeeOutput output;
	if (m.startsWith("tSNE ")) {
		auto [indices, distances] = precomputeDistances();
		precomputed_distance_callback d(distances);
		TapkeeOutput output = parametrized.withDistance(d).embedUsing(indices);
	} else if (m.startsWith("kPCA")) {
		auto [indices, distances] = precomputeDistances(true);
		precomputed_kernel_callback k(distances);
		output = parametrized.withKernel(k).embedUsing(indices);
	} else {
		// setup feature matrix
		IndexType nrows = features[0].size();
		DenseMatrix featmat(nrows, nFeat);

		for (int i = 0; i < nFeat; ++i)
			featmat.col(i) = Eigen::Map<DenseVector>(features[i].data(), nrows);

		// calculate
		output = parametrized.embedUsing(featmat);
	}

	// store result chart-readable
	if (m.startsWith("PCA") || m.startsWith("kPCA")) {
		std::map<QString, std::pair<int, int>> map = {
		    {{m + " 12"}, {0, 1}}, {{m + " 13"}, {0, 2}}, {{m + " 23"}, {1, 2}}
		};
		QMap<QString, QVector<QPointF>> ret;
		for (const auto& [name, indices] : map) {
			auto points = QVector<QPointF>(nFeat);
			for (int i = 0; i < nFeat; ++i)
				points[i] = {output.embedding(i, indices.first), output.embedding(i, indices.second)};
			ret.insert(name, points);
		}
		return ret;
	}

	QVector<QPointF> points(nFeat);
	for (int i = 0; i < nFeat; ++i)
		points[i] = {output.embedding(i, 0), output.embedding(i, 1)};
	return {{m, points}};
}

}
