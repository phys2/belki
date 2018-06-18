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

	QString name() { return sourcename; }

	QVector<unsigned> importMarkers(const QString &filename);
	void exportMarkers(const QString &filename, const QVector<unsigned> &indices);

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void ioError(const QString &message);
	void newAnnotations(const QString &name, bool loaded=false);
	void newHierarchy(const QString &name, bool loaded=false);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void openDataset(const QString &filename);
	void readAnnotations(const QString &name);
	void readHierarchy(const QString &name);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename);

protected:
	void freadError(const QString &filename);

	void close(bool save = false);

	QString sourcename; // a file can have several source data in general, we only support/select one right now

	qzip::Zip *container = nullptr;

	Dataset &data;
};

#endif // STORAGE_H
