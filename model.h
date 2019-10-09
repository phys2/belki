#ifndef MODEL_H
#define MODEL_H

#include <QString>
#include <QColor>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <vector>
#include <unordered_map>
#include <memory>
#include <variant>

using ProteinId = unsigned; // for semantic distinction
using ProteinVec = std::vector<ProteinId>;

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
	using Ptr = std::unique_ptr<Features>;
	using Vec = std::vector<std::vector<double>>;
	struct Range {
		double scale() const { return 1./(max - min); }

		double min = 0.;
		double max = 1.;
	};

	bool hasScores() const { return !scores.empty(); }
	QStringList dimensions;

	// from protein in vectors (1:1 index) to db index
	std::vector<ProteinId> protIds;
	// from protein db to index in vectors
	std::unordered_map<ProteinId, unsigned> protIndex;

	// original data
	Vec features;
	Range featureRange;
	bool logSpace = false;

	// measurement scores
	Vec scores;
	Range scoreRange;
};

struct Annotations {
	struct Group {
		QString name;
		QColor color = {};
		// note: groups are non-exclusive
		std::vector<ProteinId> members = {};
		// mode/centroid of the cluster, if available, in the source's feature space
		std::vector<double> mode = {};
	};

	QString name;
	// source dataset
	unsigned source = 0; // 0 means none

	// group definitions
	std::unordered_map<unsigned, Group> groups;
	// order of clusters (based on size/name/etc)
	std::vector<unsigned> order;
};

struct HrClustering {
	struct Cluster {
		double distance;
		unsigned parent;
		std::vector<unsigned> children;
		std::optional<ProteinId> protein;
	};

	QString name;

	std::vector<Cluster> clusters;
};

using Structure = std::variant<Annotations, HrClustering>;

#endif // MODEL_H
