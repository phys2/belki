#ifndef DATASET_H
#define DATASET_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QFile>

class Dataset
{

public:
	Dataset(QString filename);
	~Dataset();

	struct {
		QString filename;
		qint64 size;
		QByteArray checksum;
	} source;

	QStringList dimensions;

	QMap<QString, int> labelIndex;
	QVector<QString> indexLabel;

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

	static QByteArray fileChecksum(QFile &file);
};

#endif // DATASET_H
