#include "storage.h"
#include "dataset.h"

#include "storage/qzip.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash> // for checksum
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

/* storage version, increase on breaking changes */
// TODO: when we start doing new versions, ensure we update a file we open to new version (incl. version entry)
constexpr int storage_version = 1;

Storage::Storage(ProteinDB &proteins, QObject *parent)
    : QObject(parent),
      proteins(proteins)
{
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
	if (data.peek<Dataset::Base>()->conf.parent)
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

std::unique_ptr<QTextStream> Storage::openDataset(const QString &filename)
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
		for (auto &a : qAsConst(an))
			emit newAnnotations(QFileInfo(a).completeBaseName());

		// hierarchies (clustering)
		auto hi = contents.filter(QRegularExpression("^hierarchies/.*\\.json$"));
		for (auto &h : qAsConst(hi))
			emit newHierarchy(QFileInfo(h).completeBaseName());

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
		return std::make_unique<QTextStream>(d.container->read("input/" + d.sourcename + ".tsv"));

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

		// parse -- NOTE: do not return QFile based QTextStream! QFile is on stack
		return std::make_unique<QTextStream>(f.readAll());

		// TODO: need to rethink all this crap
		// namely we want project files instead of shadow zips
		/*
		auto checksum = calc_checksum(tsv);

		auto zipname = fi.path() + "/" + sourcename + ".zip";

		container = std::make_unique<qzip::Zip>();
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

std::unique_ptr<QTextStream> Storage::readAnnotations(const QString &name)
{
	QReadLocker _(&d.l);
	return std::make_unique<QTextStream>(d.container->read("annotations/" + name + ".tsv"));
}

std::unique_ptr<QJsonObject> Storage::readHierarchy(const QString &name)
{
	d.l.lockForRead();
	auto json = QJsonDocument::fromJson(d.container->read("hierarchies/" + name + ".json"));
	d.l.unlock();
	if (json.isNull() || !json.isObject()) {
		// TODO: we could pass parsing errors with fromJson() third argument
		emit ioError("The selected file does not contain valid JSON!");
		return {};
	}
	return std::make_unique<QJsonObject>(json.object());
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
	bool success = proteins.readDescriptions(content);
	if (!success)
		return;

	// TODO: actual import (storage in ZIP, and then ability to re-read)
}

void Storage::importAnnotations(const QString &filename, const QByteArray &content)
{
	QFileInfo fi(filename);
	d.l.lockForWrite();
	d.container->write("annotations/" + fi.completeBaseName() + ".tsv", content);
	d.l.unlock();
	emit newAnnotations(fi.completeBaseName(), true);
}

void Storage::importHierarchy(const QString &filename, const QByteArray &content)
{
	QFileInfo fi(filename);
	d.l.lockForWrite();
	d.container->write("hierarchies/" + fi.completeBaseName() + ".json", content);
	d.l.unlock();
	emit newHierarchy(fi.completeBaseName(), true);
}

void Storage::exportAnnotations(const QString &filename, Dataset::ConstPtr data)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly))
		return ioError(QString("Could not write file %1!").arg(filename));

	QTextStream out(&f);
	auto b = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>();
	auto p = proteins.peek();

	// write header
	out << "Protein Name";
	for (auto& [i, c] : s->clustering.clusters)
		out << "\t" << c.name;
	out << endl;

	// write associations
	for (unsigned i = 0; i < b->protIds.size(); ++i) {
		auto &protein = p->proteins[b->protIds[i]];
		auto &m = s->clustering.memberships[i];
		out << protein.name << "_" << protein.species;
		for (auto& [i, c] : s->clustering.clusters) {
			out << "\t";
			if (m.count(i))
				out << c.name;
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
	QWriteLocker _(&d.l);
	// TODO disabled to cause no harm
	//if (container && save)
	//	container->save();
	d.container.reset();
}

void Storage::freadError(const QString &filename)
{
	emit ioError(QString("Could not read file %1!").arg(filename));
}
