#include "storage.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QCryptographicHash> // for checksum
#include <QtCore/QTextStream>
#include <QtDebug>

Storage::Storage(Dataset &data) : data(data)
{
	// TODO: this is awkward, on-demand computing
	connect(this, &Storage::newData, &data, &Dataset::computeDisplays);
}

Storage::~Storage()
{
	// TODO write/close(); // save interim results on previous dataset
}

void Storage::openDataset(const QString &filename)
{
	// TODO write/close(); // save interim results on previous dataset

	// TODO: what if we get a ZIP file

	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}

	// parse
	auto success = data.readSource(QTextStream(&f)); // obtain data and interim results
	if (!success)
		return;

	// update our metadata and zip… TODO
	meta.filename = filename;
	meta.size = f.size();
	meta.checksum = fileChecksum(&f);

	emit newData();
}

void Storage::importAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}
	data.readAnnotations(QTextStream(&f));
}

void Storage::importHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}
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
	if (!f.open(QIODevice::WriteOnly)) {
		emit ioError(QString("Could not write file %1!").arg(filename));
		return;
	}

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

QString Storage::zipName(const QString &filename)
{
	QFileInfo fi(filename);
	return fi.path() + "/" + fi.completeBaseName() + ".zip";
}

QByteArray Storage::fileChecksum(QFile *file)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(file)) {
		return hash.result();
	}
	return QByteArray();
}
