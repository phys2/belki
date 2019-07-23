#include "storage.h"
#include "dataset.h"

#include "storage/qzip.h"
#include "compute/features.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash> // for checksum
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

/* storage version, increase on breaking changes */
// TODO: when we start doing new versions, ensure we update a file we open to new version (incl. version entry)
constexpr int storage_version = 1;

Storage::Storage(ProteinDB &proteins, QObject *parent)
    : QObject(parent),
      proteins(proteins)
{
	/* hack: we will move to a project model, where the container is always
	 * present */
	d.container = std::make_unique<qzip::Zip>();
}

Storage::~Storage()
{
	close(true);
}

QString Storage::name()
{
	QReadLocker _(&d.l);
	return d.sourcename;
}

void Storage::storeDisplay(const Dataset& data, const QString &name)
{
	if (data.config().parent)
		return; // TODO: temporary hack to avoid saving from sliced data

	QWriteLocker _(&d.l);
	if (!d.container)
		return;

	auto entryname = "input/" + d.sourcename + "/displays/" + name + ".tsv";
	if (d.container->has_file(entryname))
		return; // do not save redundant copies

	auto tsv = data.exportDisplay(name);
	d.container->write(entryname, tsv);
}

Features::Ptr Storage::openDataset(const QString &filename, const QString &featureColName)
{
	close(true);

	QFileInfo fi(filename);
	auto filetype = fi.suffix().toLower();

	auto check_version = [this] (auto &zipname, auto &contents) {
		auto vs = contents.filter(QRegularExpression("^belki-[0-9]*$"));
		if (vs.empty()) {
			ioError(QString("Could not identify %1 as a Belki file!").arg(zipname));
			return false;
		}
		if (vs.constFirst().split("-").constLast().toInt() > storage_version) {
			ioError(QString("This version of Belki is too old to understand %1!").arg(zipname));
			return false;
		}
		return true;
	};
	auto check_checksum = [this] (auto &zipname, auto &contents, auto &basename, auto proof) {
		auto re = QString("^input/%1/.*\\.sha256$").arg(QRegularExpression::escape(basename));
		auto cs = contents.filter(QRegularExpression(re));
		if (cs.empty()) {
			ioError(QString("The ZIP file %1 lacks a checksum for %2!").arg(zipname, basename));
			return false;
		}
		if (QFileInfo(cs.constFirst()).completeBaseName() != proof) {
			ioError(QString("The checksum for %2 in ZIP file %1 does not match!").arg(zipname, basename));
			return false;
		}
		return true;
	};
	auto calc_checksum = [] (auto data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(); };
	auto read_auxiliary = [this] (auto &contents) {
		// displays – read at start
		auto re = QRegularExpression(
		              QString("^input/%1/displays/(?<name>.*)\\.tsv$")
		              .arg(QRegularExpression::escape(d.sourcename)));
		auto de = contents.filter(re);
		// TODO: emit newDisplay, let GUI decide whether to show it or not
		for (auto &d : qAsConst(de))
			emit newDisplay(re.match(d).captured("name"));

		// annotations
		auto an = contents.filter(QRegularExpression("^annotations/.*\\.tsv$"));
		//for (auto &a : qAsConst(an))
		//	emit newAnnotations(QFileInfo(a).completeBaseName());

		// hierarchies (clustering)
		auto hi = contents.filter(QRegularExpression("^hierarchies/.*\\.json$"));
		//for (auto &h : qAsConst(hi))
		//	emit newHierarchy(QFileInfo(h).completeBaseName());

		// todo: read markerlists
	};

	QWriteLocker _(&d.l);
	if (filetype == "zip") {
		// check version
		d.container = std::make_unique<qzip::Zip>();
		try {
			d.container->load(filename);
		} catch (std::runtime_error& e) {
			close();
			ioError(QString("Could not open %1:<p>%2</p>").arg(filename, e.what()));
			return {};
		}
		auto contents = d.container->names();

		// version check
		if (!check_version(filename, contents)) {
			close();
			return {};
		}

		// find source data
		auto in = contents.filter(QRegularExpression("^input/.*\\.tsv$"));
		if (in.empty()) {
			close();
			ioError(QString("No source dataset found in %1!").arg(filename));
			return {};
		}
		d.sourcename = QFileInfo(in.constFirst()).completeBaseName();
		QTextStream stream(d.container->read("input/" + d.sourcename + ".tsv"));
		return readSource(stream, featureColName);

		// TODO this emits lots of stuff (too early)
		// better let them ask for it when data is read
		// read_auxiliary(contents);
	} else {
		d.sourcename = fi.completeBaseName();
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly)) {
			freadError(filename);
			return {};
		}
		// we will read it multiple times
		// TODO see below auto tsv = f.readAll();

		QTextStream stream(&f);
		return readSource(stream, featureColName); // TODO

		// TODO: need to rethink all this crap
		// namely we want project files instead of shadow zips
		/*
		auto checksum = calc_checksum(tsv);

		auto zipname = fi.path() + "/" + sourcename + ".zip";

		if (QFileInfo(zipname).exists()) {
			container->load(zipname);
			auto contents = container->names();

			// version check
			if (!check_version(zipname, contents)) {
				close();
				return {};
			}

			// compare checksums
			if (!check_checksum(zipname, contents, sourcename, checksum)) {
				close();
				return {};
			}

			read_auxiliary(contents);
		} else {
			// initialize new zip
			container->setFilename(zipname);
			container->write("belki-" + QString::number(storage_version), {});
			container->write("input/" + sourcename + "/" + checksum + ".sha256", {});
			container->write("input/" + sourcename + ".tsv", tsv);
		}
		*/
	}
}

Features::Ptr Storage::readSource(QTextStream &in, const QString &featureColName)
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
		if (seen.count(name))
			emit ioError(QString("Multiples of protein '%1' found in the dataset!").arg(name));
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
	if (range.min < 0 || range.max > 1) { // simple heuristic to auto-normalize
		// cut off negative values
		range.min = 0.;
		double scale = 1.;
		if (normalize) {
			scale = 1. / (range.max - range.min);
			emit ioError(QString("Values outside expected range (instead [%1, %2])."
			                     "<br>Normalizing to [0, 1].").arg(range.min).arg(range.max));
		}

		for (auto &v : data.features) {
			std::for_each(v.begin(), v.end(), [min=range.min, scale] (double &e) {
				e = std::max(e - min, 0.) * scale;
			});
		}
	}
	data.featureRange = {0., (normalize ? 1. : range.max)};
	if (data.hasScores())
		data.scoreRange = features::range_of(data.scores);
}

QByteArray Storage::readFile(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		freadError(filename);
		return {};
	}
	return f.readAll();
}

void Storage::importDescriptions(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return freadError(filename);

	auto content = QTextStream(&f);
	// TODO: implement the reading here
	bool success = proteins.readDescriptions(content);
	if (!success)
		return;

	// TODO: actual import (storage in ZIP, and then ability to re-read)
}

void Storage::importAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		freadError(filename);
		return;
	}

	QTextStream in(&f);
	auto target = std::make_unique<Annotations>();
	target->name = QFileInfo(filename).completeBaseName();

	// we use SkipEmptyParts for chomping at the end, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() == 2 && header[1].contains("Members")) {
		/* expect name + list of proteins per-cluster per-line */

		/* build new clusters */
		unsigned groupIndex = 0;
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
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
	target->name = QFileInfo(filename).completeBaseName();

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

void Storage::exportAnnotations(const QString &filename, Dataset::ConstPtr data)
{
	// TODO: export through ProteinDB!!!! NOT Dataset!!
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

	QTextStream out(&f);
	auto b = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>();
	auto p = proteins.peek();

	// write header
	out << "Protein Name";
	for (auto& [i, g] : s->clustering.groups)
		out << "\t" << g.name;
	out << endl;

	// write associations
	for (unsigned i = 0; i < b->protIds.size(); ++i) {
		auto &protein = p->proteins[b->protIds[i]];
		auto &m = s->clustering.memberships[i];
		out << protein.name << "_" << protein.species;
		for (auto& [i, g] : s->clustering.groups) {
			out << "\t";
			if (m.count(i))
				out << g.name;
		}
		out << endl;
	}
}

void Storage::importMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		freadError(filename);
		return;
	}

	QTextStream in(&f);
	std::vector<QString> names;
	while (!in.atEnd()) {
		QString name;
		in >> name;
		names.push_back(name);
	}
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

void Storage::close(bool save)
{
	// TODO disabled to cause no harm
	//QWriteLocker _(&d.l);
	//if (container && save)
	//	container->save();
	//d.container.reset();
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

void Storage::updateColorset(QVector<QColor> colors) {
	colorset = colors;
}
