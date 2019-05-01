#include "storage.h"

#include "storage/qzip.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash> // for checksum
#include <QTextStream>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtDebug>

/* storage version, increase on breaking changes */
// TODO: when we start doing new versions, ensure we update a file we open to new version (incl. version entry)
constexpr int storage_version = 1;

Storage::Storage(Dataset &data)
    : data(data)
{
	connect(&data, &Dataset::newDisplay, this, &Storage::storeDisplay);
}

Storage::~Storage()
{
	close(true);
}

void Storage::storeDisplay(const QString &name)
{
	if (!container)
		return;

	if (data.d->conf.parent < 0)
		return; // TODO: temporary hack to avoid saving from sliced data

	auto entryname = "input/" + sourcename + "/displays/" + name + ".tsv";
	if (container->has_file(entryname))
		return; // do not save redundant copies

	auto tsv = data.writeDisplay(name);
	container->write(entryname, tsv);
}

void Storage::openDataset(const QString &filename)
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
		              .arg(QRegularExpression::escape(sourcename)));
		auto de = contents.filter(re);
		for (auto &d : qAsConst(de))
			data.readDisplay(re.match(d).captured("name"), container->read(d));

		// annotations
		auto an = contents.filter(QRegularExpression("^annotations/.*\\.tsv$"));
		for (auto &a : qAsConst(an))
			emit newAnnotations(QFileInfo(a).completeBaseName());

		// hierarchies (clustering)
		auto hi = contents.filter(QRegularExpression("^hierarchies/.*\\.json$"));
		for (auto &h : qAsConst(hi))
			emit newHierarchy(QFileInfo(h).completeBaseName());

		// todo: read markerlists
	};

	if (filetype == "zip") {
		// check version
		container = new qzip::Zip; // TODO use class member
		try {
			container->load(filename);
		} catch (std::runtime_error& e) {
			close();
			return ioError(QString("Could not open %1:<p>%2</p>").arg(filename, e.what()));
		}
		auto contents = container->names();

		// version check
		if (!check_version(filename, contents)) {
			close();
			return;
		}

		// find source data
		auto in = contents.filter(QRegularExpression("^input/.*\\.tsv$"));
		if (in.empty()) {
			close();
			return ioError(QString("No source dataset found in %1!").arg(filename));
		}
		sourcename = QFileInfo(in.constFirst()).completeBaseName();
		// parse
		auto success = data.readSource(QTextStream(container->read("input/" + sourcename + ".tsv")));
		if (!success) {
			close();
			return;
		}

		read_auxiliary(contents);
	} else {
		sourcename = fi.completeBaseName();
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly))
			return freadError(filename);
		// we will read it multiple times
		auto tsv = f.readAll();

		// parse
		auto success = data.readSource(QTextStream(tsv));
		if (!success)
			return;
		auto checksum = calc_checksum(tsv);

		auto zipname = fi.path() + "/" + sourcename + ".zip";

		container = new qzip::Zip;
		if (QFileInfo(zipname).exists()) {
			container->load(zipname);
			auto contents = container->names();

			// version check
			if (!check_version(zipname, contents)) {
				close();
				return;
			}

			// compare checksums
			if (!check_checksum(zipname, contents, sourcename, checksum)) {
				close();
				return;
			}

			read_auxiliary(contents);
		} else {
			// initialize new zip
			container->setFilename(zipname);
			container->write("belki-" + QString::number(storage_version), {});
			container->write("input/" + sourcename + "/" + checksum + ".sha256", {});
			container->write("input/" + sourcename + ".tsv", tsv);
		}
	}

	// compute some initial displays
	data.computeDisplays();
}

void Storage::readAnnotations(const QString &name)
{
	auto content = container->read("annotations/" + name + ".tsv");
	data.readAnnotations(content);
}

void Storage::readHierarchy(const QString &name)
{
	auto content = container->read("hierarchies/" + name + ".json");
	data.readHierarchy(content);
}

void Storage::importDescriptions(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return freadError(filename);

	auto content = f.readAll();
	bool success = data.readDescriptions(content);
	if (!success)
		return;

	// TODO: actual import (storage in ZIP, and then ability to re-read)
}

void Storage::importAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return freadError(filename);

	/* note: we try parsing first before storing and notifying the world about it
	 * this way we don't end up with corrupt files in our ZIP. */

	auto content = f.readAll();
	bool success = data.readAnnotations(content);
	if (!success)
		return;

	QFileInfo fi(filename);
	container->write("annotations/" + fi.completeBaseName() + ".tsv", content);
	emit newAnnotations(fi.completeBaseName(), true);
}

void Storage::importHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return freadError(filename);

	auto content = f.readAll();
	bool success = data.readHierarchy(content);
	if (!success)
		return;

	QFileInfo fi(filename);
	container->write("hierarchies/" + fi.completeBaseName() + ".json", content);
	emit newHierarchy(fi.completeBaseName(), true);
}

void Storage::exportAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

	QTextStream out(&f);
	auto d = data.peek(); // TODO: avoid acquiring read lock in writer thread!

	// write header
	out << "Protein Name";
	for (auto& [i, c] : d->clustering.clusters)
		out << "\t" << c.name;
	out << endl;

	// write associations
	for (unsigned i = 0; i < d->proteins.size(); ++i) {
		auto &p = d->proteins[i];
		auto &m = d->clustering.memberships[i];
		out << p.name << "_" << p.species;
		for (auto& [i, c] : d->clustering.clusters) {
			out << "\t";
			if (m.count(i))
				out << c.name;
		}
		out << endl;
	}
}

QVector<unsigned> Storage::importMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		freadError(filename);
		return {};
	}

	QVector<unsigned> ret;
	QTextStream in(&f);
	// TODO: it is not obvious here but this method is not called from writer thread!
	auto d = data.peek();
	while (!in.atEnd()) {
		QString name;
		in >> name;
		try {
			ret.append(d->find(name));
		} catch (std::out_of_range&) {
			qDebug() << "Ignored" << name << "(unknown)";
		}
	}
	return ret;
}

void Storage::exportMarkers(const QString &filename, const QVector<unsigned> &indices)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

	QTextStream out(&f);
	// TODO: it is not obvious here but this method is not called from writer thread!
	auto d = data.peek();
	for (auto i : indices) {
		auto &p = d->proteins[i];
		out << p.name;
		if (!p.species.isEmpty())
			out << "_" << p.species;
		out << endl;
	}
}

void Storage::close(bool save)
{
	if (container && save)
		container->save();

	delete container;
	container = nullptr;
}

void Storage::freadError(const QString &filename)
{
	emit ioError(QString("Could not read file %1!").arg(filename));
}
