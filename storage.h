#ifndef STORAGE_H
#define STORAGE_H

#include <QObject>
#include "dataset.h"

class Storage : public QObject
{
	Q_OBJECT
public:
	explicit Storage(Dataset &data);

	QVector<unsigned> importMarkers(const QString &filename);
	void exportMarkers(const QString &filename, const QVector<unsigned> &indices);

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);

protected:
	Dataset &data;
};

#endif // STORAGE_H
