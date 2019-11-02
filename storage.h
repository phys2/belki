#ifndef STORAGE_H
#define STORAGE_H

#include "utils.h"
#include "model.h"

#include <QObject>
#include <QVector>
#include <QColor>
#include <memory>

namespace qzip { class Zip; }
class ProteinDB;
class Dataset;
class QTextStream;
class QJsonDocument;
class QCborValue;

class Storage : public QObject
{
	Q_OBJECT
public:
	Storage(ProteinDB &proteins, QObject *parent = nullptr);
	~Storage();

	QString name();

	Features::Ptr openDataset(const QString &filename, const QString &featureColName = "Dist");

	Features::Ptr readSource(QTextStream &in, const QString &featureColName);
	QByteArray readFile(const QString &filename);

	void importMarkers(const QString &filename);
	void exportMarkers(const QString &filename);

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void ioError(const QString &message, MessageType type = MessageType::CRITICAL);
	void newDisplay(const QString &name, bool loaded=false);

public slots:
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename, const Annotations &source);

	void saveProjectAs(const QString &filename, std::vector<std::shared_ptr<const Dataset>> snapshot);

protected:
	Features::Ptr readSimpleSource(QTextStream &in, bool normalize);
	void finalizeRead(Features &data, bool normalize);

	QCborValue serializeDataset(std::shared_ptr<const Dataset> src);
	QCborValue serializeProteinDB();
	QCborValue serializeStructure(const Structure &src);

	void storeDisplay(const Dataset &data, const QString& name);

	void close(bool save = false);
	void freadError(const QString &filename);
	static QStringList trimCrap(QStringList values);

	struct : public RWLockable {
		QString sourcename; // a file can have several source data in general, we only support/select one right now
		std::unique_ptr<qzip::Zip> container;
	} d;

	ProteinDB &proteins;
};

#endif // STORAGE_H
