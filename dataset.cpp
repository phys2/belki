#include "dataset.h"
#include "dimred.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QMessageBox>

#include <QDebug>

void Dataset::loadDataset(const QString &filename)
{
	write(); // save interim results on previous dataset

	source = {filename, {}, {}};

	auto success = read(); // obtain data and interim results
	if (!success)
		return;

	// TODO: compute on-demand
	const std::vector<QString> available = {"PCA12", "PCA13", "PCA23", "tSNE"};
	for (auto &m : available) {
		if (!d.display.contains(m)) {
			auto result = dimred::compute(m, d.features);
			l.lockForWrite();
			d.display[m] = std::move(result);
			l.unlock();
		}
	}

	emit newData(filename);
}

void Dataset::loadAnnotations(const QString &filename)
{
	QFile f(source.filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}

	QTextStream in(&f);
	//dimensions = in.readLine().split("\t", QString::SkipEmptyParts);
}

QVector<int> Dataset::loadMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return {};
	}

	/* public method -> called from other thread -> we must lock! */
	l.lockForRead();
	QVector<int> ret;
	QTextStream in(&f);
	while (!in.atEnd()) {
		QString name;
		in >> name;

		auto it = d.protIndex.find(name);
		if (it == d.protIndex.end()) {
			qDebug() << "Ignored" << name << "(unknown)";
			continue;
		}
		ret.append(it.value());
	}
	l.unlock();
	return ret;
}

void Dataset::saveMarkers(const QString &filename, const QVector<int> indices)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly)) {
		emit ioError(QString("Could not write file %1!").arg(filename));
		return;
	}

	/* public method -> called from other thread -> we must lock! */
	l.lockForRead();
	QTextStream out(&f);
	for (auto i : indices) {
		out << d.proteins[i].name << endl;
	}
	l.unlock();
}

bool Dataset::read()
{
	// get the source data
	auto success = readSource();
	if (!success)
		return false;

	// try to read pre-computed results
	QFile f(qvName());
	if (!f.open(QIODevice::ReadOnly)) {
		qDebug() << "No serialized interim results found";
		return true;
	}

	QDataStream in(&f);
	qint64 size;
	in >> size;
	if (size != source.size) {
		qDebug() << "Interim results not compatible with file (size)";
		return true;
	}

	QByteArray checksum;
	in >> checksum;
	if (checksum != source.checksum) {
		qDebug() << "Interim results not compatible with file (checksum)";
		return true;
	}

	l.lockForWrite();
	in >> d.display;
	l.unlock();
	return true;
}

bool Dataset::readSource()
{
	QFile f(source.filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(source.filename));
		return false;
	}

	l.lockForWrite();
	d.proteins.clear();
	d.protIndex.clear();
	d.features.clear();
	d.featurePoints.clear();

	QTextStream in(&f);
	d.dimensions = in.readLine().split("\t", QString::SkipEmptyParts);
	auto len = d.dimensions.size();
	auto index = 0;
	while (!in.atEnd()) {
		QString name;
		in >> name;
		if (name.length() < 1)
			break; // early EOF
		auto parts = name.split("_");
		Protein p{name, parts.first(), parts.last(), {}};

		QVector<double> coeffs(len);
		QVector<QPointF> points(len);
		for (int i = 0; i < len; ++i) {
			in >> coeffs[i];
			points[i] = {(qreal)i, coeffs[i]};
		}
		d.features.append(std::move(coeffs));
		d.featurePoints.append(std::move(points));
		d.proteins.append(std::move(p));
		d.protIndex[name] = index;
		index++;
	}
	qDebug() << "read" << d.features.size() << "rows with" << len << "columns";
	l.unlock();

	source.size = f.size();
	source.checksum = fileChecksum(&f);

	return true;
}

void Dataset::write()
{
	if (source.filename.isEmpty() || !source.size)
		return; // no data loaded

	QFile f(qvName());
	f.open(QIODevice::WriteOnly);
	QDataStream out(&f);
	out << source.size;
	out << source.checksum;
	out << d.display;
	qDebug() << "Saved interim results to" << f.fileName();
}

QString Dataset::qvName()
{
	QFileInfo fi(source.filename);
	return fi.path() + "/" + fi.completeBaseName() + ".qv";
}

QByteArray Dataset::fileChecksum(QFile *file)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(file)) {
		return hash.result();
	}
	return QByteArray();
}
