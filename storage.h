#ifndef STORAGE_H
#define STORAGE_H

#include "utils.h"

#include <QObject>
#include <memory>

namespace qzip { class Zip; }
class ProteinDB;
class Dataset;
class QTextStream;
class QJsonDocument;

class Storage : public QObject
{
	Q_OBJECT
public:
	Storage(ProteinDB &proteins, QObject *parent = nullptr);
	~Storage();

	QString name();

	std::unique_ptr<QTextStream> openDataset(const QString &filename);
	std::unique_ptr<QTextStream> readAnnotations(const QString &name);
	std::unique_ptr<QJsonObject> readHierarchy(const QString &name);

	QByteArray readFile(const QString &filename);

	void importMarkers(const QString &filename);
	void exportMarkers(const QString &filename);

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void ioError(const QString &message);
	void newDisplay(const QString &name, bool loaded=false);
	void newAnnotations(const QString &name, bool loaded=false);
	void newHierarchy(const QString &name, bool loaded=false);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename, const QByteArray &content);
	void importHierarchy(const QString &filename, const QByteArray &content);
	void exportAnnotations(const QString &filename, std::shared_ptr<Dataset const> data);

protected:
	void storeDisplay(const Dataset &data, const QString& name);
	void freadError(const QString &filename);

	void close(bool save = false);

	struct : public RWLockable {
		QString sourcename; // a file can have several source data in general, we only support/select one right now
		std::unique_ptr<qzip::Zip> container;
	} d;

	ProteinDB &proteins;
};

#endif // STORAGE_H
