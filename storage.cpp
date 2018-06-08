#include "storage.h"

#include "storage/qzip.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QCryptographicHash> // for checksum
#include <QtCore/QTextStream>
#include <QtCore/QRegularExpression>
#include <QtDebug>

/* storage version, increase on breaking changes */
// TODO: when we start doing new versions, ensure we update a file we open to new version (incl. version entry)
constexpr int storage_version = 1;

Storage::Storage(Dataset &data)
    : data(data)
{
	connect(&data, &Dataset::newDisplay, [this] (auto name) {
		if (!container)
			return;

		auto entryname = "input/" + sourcename + "/displays/" + name + ".tsv";
		if (container->has_file(entryname))
			return; // do not save redundant copies

		auto tsv = this->data.writeDisplay(name);
		container->write(entryname, tsv);
	});
}

Storage::~Storage()
{
	close(true);
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
		auto cs = contents.filter(QRegularExpression("^input/" + basename + "/.*\\.sha256$"));
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
		auto de = contents.filter(QRegularExpression("^input/" + sourcename + "/displays/.*\\.tsv$"));
		for (auto &d : qAsConst(de))
			data.readDisplay(QFileInfo(d).completeBaseName(), container->read(d));

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

	// TODO: this is awkward, on-demand computing
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
	container->write("annotations/" + fi.fileName(), content);
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
	container->write("hierarchies/" + fi.fileName(), content);
	emit newHierarchy(fi.completeBaseName(), true);
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
