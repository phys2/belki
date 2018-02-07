#include "dataset.h"
#include "dimred.h"

#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <QCryptographicHash>
#include <QRegularExpression>

#include <QDebug>

Dataset::Dataset(QString filename)
    : source({filename, {}, {}})
{
	read(); // obtain data and interim results

	// TODO: load cluster labels

	// TODO: compute on-demand
	std::vector<QString> available = {"PCA12", "PCA13", "PCA23", "tSNE"};
	for (auto &m : available) {
		if (!display.contains(m))
			display[m] = dimred::compute(m, features);
	}

	// TODO in MainWindow: also offer dim-wise scatter plots
}

Dataset::~Dataset()
{
	write(); // save interim results
}

void Dataset::read()
{
	// get the source data
	auto success = readSource();
	if (!success)
		return;

	// try to read pre-computed results
	QFile f(qvName());
	if (!f.open(QIODevice::ReadOnly)) {
		qDebug() << "No serialized interim results found";
		return;
	}

	QDataStream in(&f);
	qint64 size;
	in >> size;
	if (size != source.size) {
		qDebug() << "Interim results not compatible with file (size)";
		return;
	}

	QByteArray checksum;
	in >> checksum;
	if (checksum != source.checksum) {
		qDebug() << "Interim results not compatible with file (checksum)";
		return;
	}

	in >> display;
}

bool Dataset::readSource()
{
	QFile f(source.filename);
	if (!f.open(QIODevice::ReadOnly)) {
		qWarning("Couldn't open TSV file.");
		return false;
	}

	features.clear();
	featurePoints.clear();
	labelIndex.clear();

	QTextStream in(&f);
	dimensions = in.readLine().split("\t", QString::SkipEmptyParts);
	auto len = dimensions.size();
	auto index = 0;
	while (!in.atEnd()) {
		QString name;
		in >> name;
		if (name.length() < 1)
			break; // early EOF
		name.remove(QRegularExpression("_HUMAN$|_RAT$"));

		QVector<double> coeffs(len);
		QVector<QPointF> points(len);
		for (int i = 0; i < len; ++i) {
			in >> coeffs[i];
			points[i] = {(qreal)i, coeffs[i]};
		}
		features.append(std::move(coeffs));
		featurePoints.append(std::move(points));
		indexLabel.append(name);
		labelIndex[name] = index;
		index++;
	}
	qDebug() << "read" << features.size() << "rows with" << len << "columns";

	source.size = f.size();
	source.checksum = fileChecksum(f);

	return true;
}

void Dataset::write()
{
	QFile f(qvName());
	f.open(QIODevice::WriteOnly);
	QDataStream out(&f);
	out << source.size;
	out << source.checksum;
	out << display;
}

QString Dataset::qvName()
{
	QFileInfo fi(source.filename);
	return fi.path() + "/" + fi.baseName() + ".qv";
}

QByteArray Dataset::fileChecksum(QFile &file)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(&file)) {
		return hash.result();
	}
	return QByteArray();
}
