#ifndef MODEL_H
#define MODEL_H

#include <QMetaType>
#include <QString>
#include <QColor>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>
#include <variant>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#include "utils.h" // needed with Qt<5.14 for std::map<QString,…>
#endif

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
Q_DECLARE_METATYPE(Protein)

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

struct Representations {
	// feature reduced point sets
	using Pointset = QVector<QPointF>;
	std::map<QString, Pointset> displays;
	// TODO: put distmats here
};

struct Annotations {
	struct Meta {
		enum Type {
			SIMPLE,
			MEANSHIFT,
			HIERCUT
		} type = SIMPLE;

		unsigned id = 0; // 0 means empty or special case
		QString name = {};
		// source dataset (reference for mode/centroid)
		unsigned dataset = 0; // 0 means none

		// MEANSHIFT: k parameter used in computation
		float k = 1.f;

		// HIERCUT: source hierarchy
		unsigned hierarchy = 0; // 0 means none
		// HIERCUT: granularity of the cut (#clusters as split criteria)
		unsigned granularity = 2;
	};

	struct Group {
		QString name;
		QColor color = {};
		// note: groups are non-exclusive
		std::vector<ProteinId> members = {};
		// mode/centroid of the cluster, if available, in the source's feature space
		std::vector<double> mode = {};
	};

	Meta meta;

	// group definitions
	std::unordered_map<unsigned, Group> groups;
	// order of clusters (based on size/name/etc)
	std::vector<unsigned> order;
};

struct HrClustering {
	struct Meta {
		unsigned id = 0; // 0 means empty
		QString name = {};
		// source dataset
		unsigned dataset = 0; // 0 means none
	};

	struct Cluster {
		double distance;
		unsigned parent;
		std::vector<unsigned> children;
		std::optional<ProteinId> protein;
	};

	Meta meta;

	std::vector<Cluster> clusters;
};

using Structure = std::variant<Annotations, HrClustering>;

struct ProteinRegister {
	std::vector<Protein> proteins;
	std::unordered_map<QString, ProteinId> index;

	// TODO: sort set by prot. name
	std::set<ProteinId> markers;

	std::unordered_map<unsigned, Structure> structures;
};

struct Order {
	enum Type {
		FILE,
		NAME,
		HIERARCHY,
		CLUSTERING
	} type = FILE;
	std::variant<std::monostate, Annotations::Meta, HrClustering::Meta> source = std::monostate();
};
Q_DECLARE_METATYPE(Order::Type)

#endif // MODEL_H
