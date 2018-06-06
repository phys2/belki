#ifndef STORAGE_H
#define STORAGE_H

#include <QObject>
#include "dataset.h"

namespace qzip { class Zip; }

class Storage : public QObject
{
	Q_OBJECT
public:
	explicit Storage(Dataset &data);
	~Storage();

	QString filename() { return sourcename; }

	QVector<unsigned> importMarkers(const QString &filename);
	void exportMarkers(const QString &filename, const QVector<unsigned> &indices);

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void openDataset(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);

protected:
	void close(bool save = false);

	QString sourcename; // a file can have several source data in general, we only support/select one right now

	qzip::Zip *container;

	Dataset &data;
};

#endif // STORAGE_H
