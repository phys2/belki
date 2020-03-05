#include "storage.h"
#include "dataset.h"

#include <QCborValue>
#include <QCborArray>
#include <QCborMap>
#include <QCborStreamWriter>
#include <QSaveFile>

/* storage version, increase on breaking changes */
// Note that this is local to serialize
const int storage_version = 2;
/* minimum Belki release version that can read this storage version */
// new storage version should warrant a major release
const char* minimum_version = "1.0";

void Storage::saveProjectAs(const QString &filename, std::vector<std::shared_ptr<const Dataset>> snapshot)
{
	QSaveFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

	QCborStreamWriter w(&f);
	/* compose manually, so we only use extra memory for the single chunks */
	w.startMap(4); // needs to be numbner of elements
	// note when adding anything: element keys need to be sorted in ascending order!
	w.append("Belki File Version");
	w.append(storage_version);
	w.append("Belki Release Version");
	w.append(minimum_version);

	w.append("proteindb");
	serializeProteinDB().toCbor(w);
	w.append("datasets");
	w.startArray(snapshot.size());
	for (auto &v : snapshot)
		serializeDataset(v).toCbor(w);
	w.endArray();
	w.endMap();

	f.commit();
}

QCborValue Storage::serializeDataset(std::shared_ptr<const Dataset> src)
{
	auto packConfig = [] (const DatasetConfiguration& config) {
		QCborArray bands;
		for (auto i : config.bands)
			bands.append(i);
		return QCborMap{
			{"id", config.id},
			{"name", config.name},
			{"parent", config.parent},
			{"bands", bands},
			{"scoreThreshold", config.scoreThresh}
		};
	};

	auto packFeatures = [] (const Features::Vec &src, const Features::Range &range) {
		QCborArray data;
		for (auto vec : src) {
			QCborArray cVec;
			for (auto v : vec)
				cVec.append(v);
			data.append(cVec);
		}
		return QCborMap{
			{"data", data},
			{"range", QCborArray{range.min, range.max}}
		};
	};

	auto packDisplay = [] (const QVector<QPointF> &src) {
		QCborArray ret;
		for (auto v : src)
			ret.append(QCborArray{v.x(), v.y()});
		return ret;
	};

	auto b = src->peek<Dataset::Base>();
	auto r = src->peek<Dataset::Representations>();

	QCborArray dimensions;
	for (const auto &v : qAsConst(b->dimensions))
		dimensions.append(v);
	QCborArray protIds;
	for (auto v : b->protIds)
	     protIds.append(v);

	QCborMap displays;
	for (const auto &[k, v] : r->displays) {
		displays.insert(k, packDisplay(v));
	}

	auto features = packFeatures(b->features, b->featureRange);
	features.insert({"logspace", b->logSpace});

	QCborMap ret{
		{"config", packConfig(src->config())},
		{"dimensions", dimensions},
		{"protIds", protIds},
		{"features", features},
		{"displays", displays},
	};
	if (b->hasScores())
		ret.insert({"scores", packFeatures(b->scores, b->scoreRange)});

	return ret;
}

QCborValue Storage::serializeProteinDB()
{
	auto p = proteins.peek();

	auto packProtein = [] (const Protein &src) {
		QCborMap ret{
			{"name", src.name},
			{"species", src.species},
			{"color", src.color.name()}
		};
		if (!src.description.isEmpty())
			ret.insert({"description", src.description});
		return ret;
	};

	QCborArray proteins;
	for (const auto &v : p->proteins)
		proteins.append(packProtein(v));

	QCborArray markers;
	for (auto v : p->markers)
		markers.append(v);

	QCborMap structures;
	for (const auto &[k, v] : p->structures)
		structures.insert(k, serializeStructure(v));

	return QCborMap{
		{"proteins", proteins},
		{"markers", markers},
		{"structures", structures}
	};
}

QCborValue Storage::serializeStructure(const Structure &src)
{
	auto packCluster = [] (const HrClustering::Cluster &src) {
		QCborArray children;
		for (auto v : src.children)
			children.append(v);
		QCborMap ret{
			{"distance", src.distance},
			{"parent", src.parent},
			{"children", children}
		};
		if (src.protein)
			ret.insert({"protein", src.protein.value_or(0)}); // MacOS
		return ret;
	};

	auto hr = std::get_if<HrClustering>(&src);
	if (hr) {
		QCborMap meta{{"name", hr->meta.name}};
		if (hr->meta.dataset)
			meta.insert({"dataset", hr->meta.dataset});
		QCborArray clusters;
		for (auto v : hr->clusters)
			clusters.append(packCluster(v));
		return QCborMap{
			{"type", "hierarchy"},
			{"meta", meta},
			{"clusters", clusters}
		};
	}

	auto packGroup = [] (const Annotations::Group &src) {
		QCborArray members;
		for (auto v : src.members)
			members.append(v);
		QCborArray mode;
		for (auto v : src.mode)
			mode.append(v);
		return QCborMap{
			{"name", src.name},
			{"color", src.color.name()},
			{"members", members},
			{"mode", mode}
		};
	};

	auto cl = std::get_if<Annotations>(&src);
	if (cl) {
		QCborMap meta{{"name", cl->meta.name}};
		switch (cl->meta.type) {
		case Annotations::Meta::SIMPLE: meta.insert({"type", "simple"}); break;
		case Annotations::Meta::MEANSHIFT:
			meta.insert({"type", "meanshift"});
			meta.insert({"k", cl->meta.k});
			break;
		case Annotations::Meta::HIERCUT:
			meta.insert({"type", "hiercut"});
			meta.insert({"hierarchy", cl->meta.hierarchy});
			meta.insert({"granularity", cl->meta.granularity});
			break;
		}
		if (cl->meta.dataset)
			meta.insert({"dataset", cl->meta.dataset});
		QCborMap groups;
		for (const auto &[k, v] : cl->groups)
			groups.insert(k, packGroup(v));
		return QCborMap{
			{"type", "annotations"},
			{"meta", meta},
			{"groups", groups}
		};
	}
	return QCborSimpleType::Undefined; // should not happen
}

