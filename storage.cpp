#include "storage.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtDebug>

Storage::Storage(Dataset &data) : data(data)
{
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
