#include "storage.h"
#include "proteindb.h"

#include <QFile>
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

// TODO: make this export method that writes .tsv file directly for user pleasure
void Storage::storeDisplay(const Representations::Pointset& disp, const QString &)
{
	QByteArray blob;
	QTextStream out(&blob, QIODevice::WriteOnly);
	for (auto it = disp.constBegin(); it != disp.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;
}

// TODO: this is just a gist
void Storage::readDisplay(const QString& name, QTextStream &in)
{
	Representations::Pointset data;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() != 2) {
			emit ioError(QString("Input malformed at line %2 in display %1").arg(name, data.size()+1));
			return;
		}

		data.push_back({line[0].toDouble(), line[1].toDouble()});
	}

	/* TODO if (data.size() != (int)peek<Base>()->features.size()) {
		ioError(QString("Display %1 length does not match source length!").arg(name));
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
		emit ioError("Could not parse file!<p>The first column must contain protein or group names.</p>");
		return;
	}

	proteins.addAnnotations(std::move(target), true);
}

void Storage::importHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		freadError(filename);
		return;
	}

	auto json = QJsonDocument::fromJson(f.readAll());
	if (json.isNull() || !json.isObject()) {
		// TODO: we could pass parsing errors with fromJson() third argument
		emit ioError(QString("File %1 does not contain valid JSON!").arg(filename));
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
		return ioError(QString("Could not write file %1!").arg(filename));

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
		return ioError(QString("Refusing to load too many (%1) markers.").arg(names.size()));

	proteins.importMarkers(names);
}

void Storage::exportMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

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

QTextStream Storage::openToStream(QFile *handler)
{
	if (!handler->open(QIODevice::ReadOnly)) {
		freadError(handler->fileName());
		return {};
	}
	return QTextStream(handler);
}

void Storage::freadError(const QString &filename)
{
	emit ioError(QString("Could not read file %1!").arg(filename));
}
