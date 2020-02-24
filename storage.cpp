#include "storage.h"
#include "dataset.h"

#include "compute/features.h"

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
void Storage::storeDisplay(const Dataset& data, const QString &name)
{
	QByteArray blob;
	QTextStream out(&blob, QIODevice::WriteOnly);
	auto in = data.peek<Dataset::Representation>();
	// TODO: check if display exists
	auto &disp = in->display.at(name);
	for (auto it = disp.constBegin(); it != disp.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;
}

Features::Ptr Storage::openDataset(const QString &filename, const QString &featureColName)
{
	QFile f(filename); // keep in scope
	return readSource(openToStream(&f), featureColName);
}

Features::Ptr Storage::readSource(QTextStream in, const QString &featureColName)
{
	// TODO: the featureColName argument is a hack. We probably want some "Config" struct instead
	bool normalize = featureColName.isEmpty() || featureColName == "Dist";

	auto header = in.readLine().split("\t");

	/* simple source files have first header field blank (first column is still proteins) */
	if (!header.empty() && header.first().isEmpty()) {
		in.seek(0);
		return readSimpleSource(in, normalize);
	}

	if (header.contains("") || header.removeDuplicates()) {
		emit ioError("Malformed header: Duplicate or empty columns!");
		return {};
	}
	if (header.size() == 0 || header.first() != "Protein") {
		emit ioError("Could not parse file!<p>The first column must contain protein names.</p>");
		return {};
	}
	int nameCol = header.indexOf("Pair");
	int featureCol = header.indexOf(featureColName.isEmpty() ? "Dist" : featureColName);
	int scoreCol = header.indexOf("Score");
	if (nameCol == -1 || featureCol == -1 || scoreCol == -1) {
		emit ioError("Could not parse file!<p>Not all necessary columns found.</p>");
		return {};
	}

	/* read file into Features object */
	auto ret = std::make_unique<Features>();
	std::map<QString, unsigned> dimensions;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < header.size()) {
			emit ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);

		/* determine protein index */
		size_t row; // the protein id we are altering
		auto index = ret->protIndex.find(protid);
		if (index == ret->protIndex.end()) {
			ret->protIds.push_back(protid);
			auto len = ret->protIds.size();
			ret->features.resize(len, std::vector<double>(dimensions.size()));
			ret->scores.resize(len, std::vector<double>(dimensions.size()));
			row = len - 1;
			ret->protIndex[protid] = row;
		} else {
			row = index->second;
		}

		/* determine dimension index */
		size_t col; // the dimension we are altering
		auto dIndex = dimensions.find(line[nameCol]);
		if (dIndex == dimensions.end()) {
			ret->dimensions.append(line[nameCol]);
			auto len = (size_t)ret->dimensions.size();
			for (auto &i : ret->features)
				i.resize(len);
			for (auto &i : ret->scores)
				i.resize(len);
			col = len - 1;
			dimensions[line[nameCol]] = col;
		} else {
			col = dIndex->second;
		}

		/* read coefficients */
		bool success = true;
		bool ok;
		double feat, score;
		feat = line[featureCol].toDouble(&ok);
		success = success && ok;
		score = line[scoreCol].toDouble(&ok);
		if (!success) {
			auto name = proteins.peek()->proteins[protid].name;
			auto err = QString("Stopped at protein '%1', malformed row!").arg(name);
			emit ioError(err);
			break; // avoid message flood
		}

		/* fill-in features and scores */
		ret->features[row][col] = feat;
		ret->scores[row][col] = std::max(score, 0.); // TODO temporary clipping
	}

	if (ret->features.empty() || ret->dimensions.empty()) {
		emit ioError(QString("Could not read any valid data rows from file!"));
		return {};
	}

	finalizeRead(*ret, normalize);
	return ret;
}

Features::Ptr Storage::readSimpleSource(QTextStream &in, bool normalize)
{
	auto header = in.readLine().split("\t");
	header.pop_front(); // first column (also expected to be empty)
	// allow empty fields at the end caused by Excel export
	while (header.last().isEmpty())
		header.removeLast();

	// ensure header consistency
	if (header.empty() || header.contains("") || header.removeDuplicates()) {
		emit ioError("Malformed header: Duplicate or empty columns!");
		return {};
	}

	/* read file into Features object */
	auto ret = std::make_unique<Features>();
	ret->dimensions = trimCrap(header);
	auto len = ret->dimensions.size();
	std::set<QString> seen; // names of read proteins
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < len + 1) {
			emit ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);
		auto name = proteins.peek()->proteins[protid].name;

		/* check duplicates */
		if (seen.count(name)) {
			emit ioError(QString("Stopped at multiple occurance of protein '%1'!").arg(name));
			return {};
		}
		seen.insert(name);

		/* read coefficients */
		bool success = true;
		std::vector<double> coeffs((size_t)len);
		for (int i = 0; i < len; ++i) {
			bool ok;
			coeffs[(size_t)i] = line[i+1].toDouble(&ok);
			success = success && ok;
		}
		if (!success) {
			emit ioError(QString("Stopped at protein '%1', malformed row!").arg(name));
			break; // avoid message flood
		}

		/* append */
		ret->protIndex[protid] = ret->protIds.size();
		ret->protIds.push_back(protid);
		ret->features.push_back(std::move(coeffs));
	}

	if (ret->features.empty()) {
		emit ioError(QString("Could not read any valid data rows from file!"));
		return {};
	}

	finalizeRead(*ret, normalize);
	return ret;
}

void Storage::finalizeRead(Features &data, bool normalize)
{
	/* setup ranges */
	//auto range = features::range_of(data.features, 0.99f);
	auto range = features::range_of(data.features);
	// normalize, if needed
	if (normalize && (range.min < 0 || range.max > 1)) {
		emit ioError(QString("Values outside expected range (instead [%1, %2])."
		                     "<br>Cutting of negative values and normalizing to [0, 1].")
		             .arg(range.min).arg(range.max), MessageType::INFO);

		// cut off negative values
		range.min = 0.;

		// normalize
		double scale = 1. / (range.max - range.min);
		for (auto &v : data.features) {
			std::for_each(v.begin(), v.end(), [min=range.min, scale] (double &e) {
				e = std::max(e - min, 0.) * scale;
			});
		}
	}
	data.featureRange = (normalize ? Features::Range{0., 1.} : range);
	data.logSpace = (data.featureRange.min >= 0 && data.featureRange.max > 10000);
	if (data.hasScores())
		data.scoreRange = features::range_of(data.scores);
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

QStringList Storage::trimCrap(QStringList values)
{
	if (values.empty())
		return values;

	/* remove custom shit in our data */
	QString match("[A-Z]{2}20\\d{6}.*?\\([A-Z]{2}(?:-[A-Z]{2})?\\)_(.*?)_\\(?(?:band|o|u)(?:\\+(?:band|o|u))+\\)?_.*?$");
	values.replaceInStrings(QRegularExpression(match), "\\1");

	/* remove common prefix & suffix */
	QString reference = values.front();
	int front = reference.size(), back = reference.size();
	for (auto it = ++values.cbegin(); it != values.cend(); ++it) {
		front = std::min(front, it->size());
		back = std::min(back, it->size());
		for (int i = 0; i < front; ++i) {
			if (it->at(i) != reference[i]) {
				front = i;
				break;
			}
		}
		for (int i = 0; i < back; ++i) {
			if (it->at(it->size()-1 - i) != reference[reference.size()-1 - i]) {
				back = i;
				break;
			}
		}
	}
	match = QString("^.{%1}(.*?).{%2}$").arg(front).arg(back);
	values.replaceInStrings(QRegularExpression(match), "\\1");

	return values;
}
