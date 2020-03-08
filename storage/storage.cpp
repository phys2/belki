#include "storage.h"
#include "proteindb.h"

#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

Storage::Storage(ProteinDB &proteins, QObject *parent)
    : QObject(parent),
      proteins(proteins)
{
}

std::vector<std::shared_ptr<Dataset>> Storage::openProject(const QString &filename)
{
	auto ret = readProject(filename);
	if (!ret.empty())
		updateFilename(filename);
	return ret;
}

void Storage::saveProject(const QString &filename, std::vector<std::shared_ptr<const Dataset> > snapshot)
{
	QSaveFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return fopenError(filename, true);
	writeProject(&f, snapshot);
	f.commit();
	updateFilename(filename); // file must exist here, so do this after writing the file
}

// TODO: make this export method that writes .tsv file directly for user pleasure
void Storage::storeDisplay(const Representations::Pointset& disp, const QString &)
{
	QByteArray blob;
	QTextStream out(&blob, QIODevice::WriteOnly);
	for (auto it = disp.constBegin(); it != disp.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;
}

// TODO: this is just a gist
void Storage::readDisplay(const QString&, QTextStream &in)
{
	Representations::Pointset data;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() != 2) {
			emit message({"Could not parse file!",
			              QString("Input malformed at line %1").arg(data.size()+1)});
			return;
		}

		data.push_back({line[0].toDouble(), line[1].toDouble()});
	}

	/* TODO if (data.size() != (int)peek<Base>()->features.size()) {
		message({"Display incompatible with data.",
			     "Display length does not match source length!"});
		return;
	}*/

	// TODO: what to do with the data
}

Features::Ptr Storage::openDataset(const QString &filename, const QString &featureColName)
{
	QFile f(filename); // keep in scope
	return readSource(openToStream(&f), featureColName); // see parse_dataset.cpp
}


void Storage::importDescriptions(const QString &filename)
{
	QFile f(filename); // keep in scope
	// TODO: implement the reading here
	proteins.readDescriptions(openToStream(&f));
}

void Storage::importAnnotations(const QString &filename)
{
	QFile f(filename); // keep in scope
	auto in = openToStream(&f);
	auto target = std::make_unique<Annotations>();
	target->meta.name = QFileInfo(filename).completeBaseName();

	// we use SkipEmptyParts for chomping at the end, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() == 2 && header[1].contains("Members")) {
		/* expect name + list of proteins per-cluster per-line */

		/* build new clusters */
		unsigned groupIndex = 0;
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t", QString::SkipEmptyParts);
			if (line.size() < 2)
				continue;

			auto &group = target->groups[groupIndex]; // emplace
			group.name = line[0];
			line.removeFirst();

			for (auto &name : qAsConst(line)) {
				auto prot = proteins.add(name);
				target->groups[groupIndex].members.push_back(prot);
			}

			groupIndex++;
		}
	} else if (header.size() > 1 && header[0].contains(re)) {
		/* expect matrix layout, first column protein names */
		header.removeFirst();

		/* setup clusters */
		target->groups.reserve((unsigned)header.size());
		for (auto i = 0; i < header.size(); ++i)
			target->groups[(unsigned)i] = {header[i]};

		/* associate to clusters */
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			auto name = line[0];
			line.removeFirst();
			auto prot = proteins.add(name);
			for (auto i = 0; i < header.size(); ++i) { // run over header to only allow valid columns
				if (line[i].isEmpty() || line[i].contains(QRegularExpression("^\\s*$")))
					continue;
				target->groups[(unsigned)i].members.push_back(prot);
			}
		}
	} else {
		emit message({"Could not parse file!",
		              "The first column must contain protein or group names."});
		return;
	}

	proteins.addAnnotations(std::move(target), true);
}

void Storage::importHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		fopenError(filename);
		return;
	}

	QJsonParseError err;
	auto json = QJsonDocument::fromJson(f.readAll(), &err);
	if (json.isNull() || !json.isObject()) {
		emit message({QString("File %1 does not contain valid JSON!").arg(filename),
		             err.errorString()});
		return;
	}

	QTextStream in(&f);
	auto target = std::make_unique<HrClustering>();
	target->meta.name = QFileInfo(filename).completeBaseName();

	auto nodes = json.object()["data"].toObject()["nodes"].toObject();
	for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
		unsigned id = it.key().toUInt();
		auto node = it.value().toObject();
		if (id + 1 > target->clusters.size())
			target->clusters.resize(id + 1);

		auto &c = target->clusters[id];
		c.distance = node["distance"].toDouble();

		/* leaf: associate proteins */
		auto content = node["objects"].toArray();
		if (content.size() == 1) {
			auto name = content[0].toString();
			c.protein = proteins.add(name);
		}

		/* non-leaf: associate children */
		if (node.contains("left_child")) {
			c.children = {(unsigned)node["left_child"].toInt(),
			              (unsigned)node["right_child"].toInt()};
		}

		/* back-association */
		if (node.contains("parent"))
			c.parent = (unsigned)node["parent"].toInt();
	}

	proteins.addHierarchy(std::move(target), true);
}

void Storage::exportAnnotations(const QString &filename, const Annotations& source)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return fopenError(filename, true);

	QTextStream out(&f);
	// write header
	out << "Name\tMembers" << endl;

	// write clusters
	auto p = proteins.peek();
	for (auto groupIndex : source.order) {
		auto group = source.groups.at(groupIndex);
		out << group.name;
		for (auto protId : group.members) {
			auto protein = p->proteins[protId];
			out << "\t" << protein.name << "_" << protein.species;
		}
		out << endl;
	}
}

void Storage::updateFilename(const QString &filename)
{
	auto fi = QFileInfo(filename);
	auto name = fi.completeBaseName();
	auto path = fi.canonicalFilePath(); // expects file to exist
	emit nameChanged(name, path);
}

void Storage::importMarkers(const QString &filename)
{
	QFile f(filename); // keep in scope
	auto in = openToStream(&f);
	std::vector<QString> names;
	while (!in.atEnd()) {
		QString name;
		in >> name;
		if (!name.isEmpty())
			names.push_back(name);
	}

	if (names.size() > 500)
		return message({QString("Refusing to load too many (%1) markers.").arg(names.size())});

	proteins.importMarkers(names);
}

void Storage::exportMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return fopenError(filename, true);

	auto p = proteins.peek();
	QTextStream out(&f);
	for (auto id : p->markers) {
		auto protein = p->proteins[id];
		out << protein.name;
		if (!protein.species.isEmpty())
			out << "_" << protein.species;
		out << endl;
	}
}

QTextStream Storage::openToStream(QFileDevice *handler)
{
	if (!handler->open(QIODevice::ReadOnly)) {
		fopenError(handler->fileName());
		return {};
	}
	return QTextStream(handler);
}

void Storage::fopenError(const QString &filename, bool write)
{
	QString format(write? "Could not write file %1!" : "Could not read file %1!");
	emit message({format.arg(filename)});
}
