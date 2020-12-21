#include "dimred.h"
#include <tapkee/tapkee.hpp> // includes Eigen
#include <tapkee/callbacks/precomputed_callbacks.hpp>
#include <tapkee/utils/logging.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp> // for EMD
#include <tbb/parallel_for.h>

#include <map>
#include <iostream>

using namespace tapkee;

namespace dimred {

const std::vector<dimred::Method> &availableMethods()
{
	static std::vector<dimred::Method> ret{
		{"PCA", "PCA/12", "Principal Component Analysis"},
#ifdef EXPERIMENTAL
		{"kPCA EMD", "kPCA EMD/12", "Kernel-PCA, EMD"},
		{"kPCA L1", "kPCA L1/12", "Kernel-PCA, Manhattan"},
		{"kPCA L2", "kPCA L2/12", "Kernel-PCA, Euclidean"},
		{"MDS L1", "MDS L1/12", "Multi-dimensional Scaling, Manhattan"},
		{"MDS NL2", "MDS NL2/12", "Multi-dimensional Scaling, Normalized L2"},
		{"MDS EMD", "MDS EMD/12", "Multi-dimensional Scaling, EMD"},
#endif
		{"tSNE", "tSNE", "t-distributed stochastic neighbor embedding"},
#ifdef EXPERIMENTAL
		{"tSNE 10", "tSNE 10", "t-SNE with perplexity 10"},
		{"tSNE 20", "tSNE 20", "t-SNE with perplexity 20"},
#endif
		{"tSNE 30", "tSNE 30", "t-SNE with perplexity 30"},
		{"tSNE 40", "tSNE 40", "t-SNE with perplexity 40"},
		{"tSNE 50", "tSNE 50", "t-SNE with perplexity 50"},
		{"tSNE 60", "tSNE 60", "t-SNE with perplexity 60"},
		{"tSNE 70", "tSNE 70", "t-SNE with perplexity 70"},
		{"Diffusion Map", "Diffusion Map", "Diffusion Map"},
#ifdef EXPERIMENTAL
		{"Diff. Map L1", "Diff. Map L1", "Diffusion Map, Manhattan"},
		{"Diff. Map EMD", "Diff. Map EMD", "Diffusion Map, EMD"},
#endif
	};
	return ret;
}

QMap<QString, QVector<QPointF>> compute(QString m, const std::vector<std::vector<double>> &features)
{
	if (features.empty() || features.front().size() < 3)
		return {};
	std::cout << "Computing " << m.toStdString() << std::endl;

	// setup some logging
	tapkee::LoggingSingleton::instance().enable_info();
	tapkee::LoggingSingleton::instance().enable_benchmark();

	// perform dimensionality reduction
	ParametersSet p;
	if (m.startsWith("PCA") || m.startsWith("kPCA") || m.startsWith("MDS")) {
		p, method=(m == "PCA" ? PCA : (m.startsWith("kPCA") ? KernelPCA : MultidimensionalScaling));
		p, target_dimension=std::min(size_t(3), features.front().size() - 1);
	}
	if (m.startsWith("tSNE")) {
		p, method=tDistributedStochasticNeighborEmbedding, target_dimension=2;
		double perp = m.split(' ').back().toDouble();
		if (perp > 0.)
			p, sne_perplexity=perp;
		// also available: sne_theta
	}
	if (m.startsWith("Diff")) {
		p, method=DiffusionMap, target_dimension=2;
	}
	auto parametrized = initialize().withParameters(p);
	auto nFeat = features.size();

	std::map<QString, std::function<double(size_t, size_t)>> distFun = {
		{"L1", [&features] (size_t i, size_t j) {
			return cv::norm(features[i], features[j], cv::NORM_L1);
		}},
		{"L2", [&features] (size_t i, size_t j) {
			return cv::norm(features[i], features[j], cv::NORM_L2);
		}},
		{"NL2", [&features] (size_t i, size_t j) {
			cv::Mat1d mi(features[i]), mj(features[j]);
			mi /= cv::norm(mi);
			mj /= cv::norm(mj);
			return cv::norm(mi, mj); // TODO: use cv::NORM_L2SQR?
		}},
		{"COS", [&features] (size_t i, size_t j) {
			cv::Mat1d mi(features[i]), mj(features[j]);
			return mi.dot(mj) / (cv::norm(mi) * cv::norm(mj));
		}},
		{"EMD", [&features] (size_t i, size_t j) {
			cv::Mat1f mi(features[i].size(), 1 + 1, 1.f); // weight + value
			cv::Mat1f mj(features[j].size(), 1 + 1, 1.f); // weight + value
			std::copy(features[i].cbegin(), features[i].cend(), mi.col(1).begin());
			std::copy(features[j].cbegin(), features[j].cend(), mj.col(1).begin());
			// use L1 here as we have scalar inputs anyway
			return cv::EMD(mi, mj, cv::DIST_L1);
		}},
	};

	auto precomputeDistances = [&] (auto callback, bool kernel = false) {
		std::vector<IndexType> indices((unsigned)nFeat);
		DenseMatrix distances(nFeat, nFeat);

		std::cerr << "computing distances for " << nFeat << " points" << std::endl;
		tbb::parallel_for(size_t(0), nFeat, [&] (size_t i) {
			if (i % 10)
				std::cerr << ".";
			indices[i] = i;

			// fill diagonal
			distances(i, i) = 0;

			for (size_t j = i + 1; j < nFeat; ++j) {

				auto dist = callback(i, j);
				// fill symmetrically
				distances(j, i) = distances(i, j) = dist;
			}
		});
		std::cerr << " done" << std::endl;
		if (kernel) {
			auto imean = -1. / distances.mean();
			distances = (distances.array() * imean).exp();
		}
		return std::make_pair(indices, distances);
	};

	TapkeeOutput output;
	// custom distance
	if (m.startsWith("MDS") || m.startsWith("Diff. Map")) {
		auto [indices, distances] = precomputeDistances(distFun[m.split(" ").last()]);
		precomputed_distance_callback d(distances);
		output = parametrized.withDistance(d).embedUsing(indices);
	// custom kernel
	} else if (m.startsWith("kPCA")) {
		auto [indices, distances] = precomputeDistances(distFun[m.split(" ").last()], true);
		precomputed_kernel_callback k(distances);
		output = parametrized.withKernel(k).embedUsing(indices);
	// plain work on features
	} else {
		// setup feature matrix
		IndexType nrows = features[0].size();
		DenseMatrix featmat(nrows, nFeat);
		for (size_t i = 0; i < nFeat; ++i)
			featmat.col(i) = Eigen::Map<const DenseVector>(features[i].data(), nrows);

		output = parametrized.embedUsing(featmat);
	}

	// store result chart-readable: 3D → 2D
	if (output.embedding.cols() == 3) {
		// hack: ensure the name does not yet contain dimension markers
		m = m.split("/").first();
		std::map<QString, std::pair<int, int>> map = {
		    {{m + "/12"}, {0, 1}}, {{m + "/13"}, {0, 2}}, {{m + "/23"}, {1, 2}}
		};
		QMap<QString, QVector<QPointF>> ret;
		for (const auto& [name, cols] : map) {
			auto points = QVector<QPointF>(nFeat);
			for (size_t i = 0; i < nFeat; ++i)
				points[i] = {output.embedding(i, cols.first), output.embedding(i, cols.second)};
			ret.insert(name, points);
		}
		return ret;
	}

	// plain 2D
	QVector<QPointF> points(nFeat);
	for (size_t i = 0; i < nFeat; ++i)
		points[i] = {output.embedding(i, 0), output.embedding(i, 1)};
	return {{m, points}};
}

}
