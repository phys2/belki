#include "dataset.h"

#include <tapkee/tapkee.hpp> // for Eigen

#include <QDebug>

using namespace tapkee;

namespace dimred {

QVector<QPointF> compute(QString m, QVector<QVector<double> > &features)
{
	// setup the matrix
	Eigen::Index nrows = features[0].size();
	Eigen::Index ncols = features.size();
	Eigen::MatrixXd featmat(nrows, ncols);

	for (int i = 0; i < ncols; ++i)
		featmat.col(i) = Eigen::Map<Eigen::VectorXd>(features[i].data(), nrows);

	qDebug() << "Computing" << m;

	// perform dimensionality reduction
	ParametersSet p;
	if (m == "PCA12") {
		p, method=PCA, target_dimension=2;
	}
	if (m == "PCA13") {
		p, method=PCA, target_dimension=3;
	}
	if (m == "PCA23") {
		p, method=PCA, target_dimension=3;
	}
	if (m == "tSNE") {
		p, method=tDistributedStochasticNeighborEmbedding, target_dimension=2;
	}

	TapkeeOutput output = initialize()
	    .withParameters(p)
	    .embedUsing(featmat);

	// store result chart-readable
	QVector<QPointF> points(ncols);
	for (int i = 0; i < ncols; ++i)
		if (m == "PCA13")
			points[i] = {output.embedding(i, 0), output.embedding(i, 2)};
		else
			points[i] = {output.embedding(i, 0), output.embedding(i, 1)};

	return points;
}

}
