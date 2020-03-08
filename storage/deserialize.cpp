#include "storage.h"
#include "dataset.h"

#include <QCborValue>
#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QFile>

template<>
Structure Storage::deserializeStructure<2>(const QCborMap &source, unsigned id)
{
	auto unpackCluster = [] (const QCborMap& src) {
		HrClustering::Cluster ret;
		ret.distance = src.value("distance").toInteger();
		ret.parent = src.value("parent").toInteger();
		for (auto i : src.value("children").toArray())
			ret.children.push_back(i.toInteger());
		if (src.contains(QString{"protein"}))
			ret.protein = src.value("protein").toInteger();
		return ret;
	};

	auto unpackGroup = [] (const QCborMap& src) {
		Annotations::Group ret;
		ret.name = src.value("name").toString();
		ret.color = src.value("color").toString();
		for (auto i : src.value("members").toArray())
			ret.members.push_back(i.toInteger());
		for (auto i : src.value("mode").toArray())
			ret.mode.push_back(i.toDouble());
		return ret;
	};

	auto type = source.value("type").toString();
	if (type == "hierarchy") {
		HrClustering ret;
		auto meta = source.value("meta").toMap();
		ret.meta.id = id;
		ret.meta.name = meta.value("name").toString();
		ret.meta.dataset = meta.value("parent").toInteger(0); // optional, default 0
		for (auto i : source.value("clusters").toArray())
			ret.clusters.push_back(unpackCluster(i.toMap()));
		return ret;
	}
	if (type == "annotations") {
		Annotations ret;
		auto meta = source.value("meta").toMap();
		std::map<QString, Annotations::Meta::Type> typeMap{
			{"simple", Annotations::Meta::SIMPLE},
			{"meanshift", Annotations::Meta::MEANSHIFT},
			{"hiercut", Annotations::Meta::HIERCUT},
		};
		ret.meta.type = typeMap.at(meta.value("type").toString());
		ret.meta.id = id;
		ret.meta.name = meta.value("name").toString();
		ret.meta.dataset = meta.value("parent").toInteger(0); // optional, default 0
		// individual parameters are set depending on type, but we can just default
		ret.meta.k = meta.value("k").toDouble(1.);
		ret.meta.hierarchy = meta.value("hierarchy").toInteger(0);
		ret.meta.granularity = meta.value("granularity").toInteger(0);

		for (const auto &[k, v] : source.value("groups").toMap())
			ret.groups[k.toInteger()] = unpackGroup(v.toMap());
		annotations::order(ret, (type != Annotations::Meta::SIMPLE));

		return ret;
	}
	return {};
}

template<>
void Storage::deserializeProteinDB<2>(const QCborMap &source)
{
	auto unpackProtein = [] (const QCborMap& src) {
		Protein ret;
		ret.name = src.value("name").toString();
		ret.species = src.value("species").toString();
		ret.color = src.value("color").toString();
		if (src.contains(QString{"description"}))
			ret.description = src.value("description").toString();
		return ret;
	};

	auto target = std::make_unique<ProteinDB::Public>();
	for (auto i : source.value("proteins").toArray()) {
		auto protein = unpackProtein(i.toMap());
		target->index[protein.name] = target->proteins.size();
		target->proteins.push_back(protein);
	}
	for (auto i : source.value("markers").toArray()) {
		target->markers.insert(i.toInteger());
	}
	for (const auto &[k, v] : source.value("structures").toMap()) {
		auto structure = deserializeStructure<2>(v.toMap(), k.toInteger());
		target->structures[k.toInteger()] = structure;
	}
	proteins.init(std::move(target));
}

template<>
std::shared_ptr<Dataset> Storage::deserializeDataset<2>(const QCborMap &source)
{
	auto unpackConfig = [] (const QCborMap& src) {
		DatasetConfiguration ret;
		auto bands = src.value("bands").toArray();
		ret.id = src.value("id").toInteger();
		ret.parent = src.value("parent").toInteger();
		ret.name = src.value("name").toString();
		for (auto i : bands)
			ret.bands.push_back(i.toInteger());
		ret.scoreThresh = src.value("scoreThreshold").toDouble();
		return ret;
	};

	auto unpackDisplay = [] (const QCborArray& src) {
		Representations::Pointset ret;
		for (auto i : src) {
			auto arr = i.toArray();
			ret.append({arr.first().toDouble(), arr.last().toDouble()});
		}
		return ret;
	};

	auto importFeats = [] (const QCborMap& src, Features::Vec &data, Features::Range &range) {
		for (auto vec : src.value("data").toArray()) {
			std::vector<double> row;
			for (auto v : vec.toArray())
				row.push_back(v.toDouble());
			data.push_back(row);
		}
		auto irange = src.value("range").toArray();
		range.min = irange.first().toDouble();
		range.max = irange.last().toDouble();
	};

	auto config = unpackConfig(source.value("config").toMap());

	auto features = std::make_unique<Features>();
	auto ifeats = source.value("features").toMap();
	importFeats(ifeats, features->features, features->featureRange);
	features->logSpace = ifeats.value("logspace").toBool();
	for (auto dim : source.value("dimensions").toArray())
		features->dimensions.push_back(dim.toString());
	for (auto pId : source.value("protIds").toArray())
		features->protIds.push_back(pId.toInteger());
	if (source.contains(QString{"scores"}))
		importFeats(source.value("scores").toMap(), features->scores, features->scoreRange);

	auto repr = std::make_unique<Representations>();
	if (source.contains(QString{"displays"})) {
		for (const auto &[name, points] : source.value("displays").toMap()) {
			repr->displays[name.toString()] = unpackDisplay(points.toArray());
		}
	}

	auto dataset = std::make_shared<Dataset>(proteins, config);
	dataset->spawn(std::move(features), std::move(repr));
	return dataset;
}

template<>
std::vector<std::shared_ptr<Dataset>> Storage::deserializeProject<2>(const QCborMap &top) {
	// TODO: From here on, we expect a valid layout. Add checks where needed

	deserializeProteinDB<2>(top.value("proteindb").toMap());

	auto datasets = top.value("datasets").toArray();
	std::vector<std::shared_ptr<Dataset>> ret;
	for (auto &i : qAsConst(datasets))
		ret.push_back(deserializeDataset<2>(i.toMap()));
	return ret;
}

std::vector<std::shared_ptr<Dataset>> Storage::readProject(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		ioError(QString("Could not open file %1!").arg(filename));
		return {};
	}
	QCborStreamReader r(&f);

	/* We expect a map with version etc. on top level */
	if (r.isTag() && r.toTag() == QCborKnownTags::Signature)
		r.next();
	auto top = QCborValue::fromCbor(r).toMap();
	if (r.lastError() != QCborError::NoError) {
		ioError(QString("Error reading file:<p>%1</p>").arg(r.lastError().toString()));
		return {};
	}
	auto version = top.value("Belki File Version");
	if (not version.isInteger()) {
		ioError("Invalid file, could not read version");
		return {};
	}

	/* dispatch for all known versions */
	if (version.toInteger(0) == 2)
		return deserializeProject<2>(top);

	/* else: version too new */
	auto minversion = top.value("Belki Release Version");
	auto msg = "File version %1 not supported.<p>Please upgrade Belki to at least version %2.</p>";
	ioError(QString(msg).arg(version.toInteger()).arg(minversion.toString("?")));
	return {};
}
