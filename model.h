#ifndef MODEL_H
#define MODEL_H

#include <QString>
#include <QColor>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <vector>
#include <unordered_map>

using ProteinId = unsigned; // for semantic distinction
struct Protein {
	// first part of protein name, used as identifier
	QString name;
	// last part of protein name
	QString species;
	// description, if any
	QString description;
	// random or user-set color
	QColor color;
};

struct Features {
	using Vec = std::vector<std::vector<double>>;
	struct Range {
		double scale() const { return 1./(max - min); }

		double min = 0.;
		double max = 1.;
	};

	bool empty() const { return features.empty() || dimensions.empty(); }
	bool hasScores() const { return !scores.empty(); }
	QStringList dimensions;

	// from protein in vectors (1:1 index) to db index
	std::vector<ProteinId> protIds;
	// from protein db to index in vectors
	std::unordered_map<ProteinId, unsigned> protIndex;

	// original data
	Vec features;
	Range featureRange;

	// measurement scores
	Vec scores;
	Range scoreRange;
};

#endif // MODEL_H
