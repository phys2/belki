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
    : container(nullptr),
      data(data)
{
	// TODO: this is awkward, on-demand computing
	//connect(this, &Storage::newData, &data, &Dataset::computeDisplays);
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

	if (filetype == "zip") {
		// check version
		container = new qzip::Zip; // TODO use class member
		container->load(filename);
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

		// todo: also read whatever else is available

	} else {
		QFile f(filename);
		if (!f.open(QIODevice::ReadOnly))
			return ioError(QString("Could not read file %1!").arg(filename));

		// parse
		auto success = data.readSource(QTextStream(&f));
		if (!success)
			return;
		auto checksum = fileChecksum(&f);
		qDebug() << checksum;

		container = new qzip::Zip;
		sourcename = fi.completeBaseName();
		auto zipname = fi.path() + "/" + sourcename + ".zip";
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

			// todo: read whatever else is available
		} else {
			container->setFilename(zipname);
			container->write("belki-" + QString::number(storage_version), {});
			container->write("input/" + sourcename + "/" + checksum + ".sha256", {});
		}
	}

	emit newData();
}

void Storage::importAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return ioError(QString("Could not read file %1!").arg(filename));

	data.readAnnotations(QTextStream(&f));
}

void Storage::importHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly))
		return ioError(QString("Could not read file %1!").arg(filename));

	data.readHierarchy(f.readAll());
}

QVector<unsigned> Storage::importMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
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

QByteArray Storage::fileChecksum(QFile *file)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(file)) {
		return hash.result();
	}
	return QByteArray();
}
