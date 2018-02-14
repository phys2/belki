#ifndef DATASET_H
#define DATASET_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>

class QFile;

class Dataset
{

public:
	struct Protein {
		// <name>_<species> as read from the data, used as identifier
		QString name;
		// first part of protein name
		QString firstName;
		// last part of protein name
		QString species;
	};

	Dataset(const QString &filename);
	~Dataset();

	QVector<int> loadMarkers(const QString &filename);
	void saveMarkers(const QString &filename, const QVector<int> indices);

	struct {
		QString filename;
		qint64 size;
		QByteArray checksum;
	} source;

	QStringList dimensions;

	QMap<QString, int> protIndex; // map indentifiers to index in vectors

	// meta data
	QVector<Protein> proteins;

	// original data
	QVector<QVector<double>> features;
	// pre-cached set of points
	QVector<QVector<QPointF>> featurePoints;

	// feature reduced point sets
	QMap<QString, QVector<QPointF>> display;

protected:
	void read();
	bool readSource();
	void write();

	QString qvName();

	static QByteArray fileChecksum(QFile *file);
};

#endif // DATASET_H
