#ifndef STORAGE_H
#define STORAGE_H

#include "utils.h"
#include "model.h"

#include <QObject>
#include <QVector>
#include <QColor>
#include <memory>

class ProteinDB;
class Dataset;
class QFile;
class QTextStream;
class QJsonDocument;
class QCborValue;

class Storage : public QObject
{
	Q_OBJECT
public:
	Storage(ProteinDB &proteins, QObject *parent = nullptr);

	std::vector<std::shared_ptr<Dataset>> openProject(const QString &filename);
	void saveProjectAs(const QString &filename, std::vector<std::shared_ptr<const Dataset>> snapshot);

	Features::Ptr openDataset(const QString &filename, const QString &featureColName = "Dist");

signals: // IMPORTANT: always provide target object pointer for thread-affinity
	void ioError(const QString &message, MessageType type = MessageType::CRITICAL);

public slots:
	void importMarkers(const QString &filename);
	void exportMarkers(const QString &filename);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename, const Annotations &source);

protected:
	// TODO put implementations in storage/…
	Features::Ptr readSource(QTextStream in, const QString &featureColName);
	Features::Ptr readSimpleSource(QTextStream &in, bool normalize);
	void finalizeRead(Features &data, bool normalize);

	// see storage/serialize.cpp
	QCborValue serializeDataset(std::shared_ptr<const Dataset> src);
	QCborValue serializeProteinDB();
	QCborValue serializeStructure(const Structure &src);

	// see storage/deserialize.cpp
	template<int VER>
	void deserializeProteinDB(const QCborMap &proteindb);
	template<int VER>
	Structure deserializeStructure(const QCborMap &structure, unsigned id);
	template<int VER>
	std::shared_ptr<Dataset> deserializeDataset(const QCborMap &dataset);
	template<int VER>
	std::vector<std::shared_ptr<Dataset>> deserializeProject(const QCborMap &top);

	// TODO dead code right now
	void storeDisplay(const Dataset &data, const QString& name);
	void readDisplay(const QString& name, QTextStream &in);

	QTextStream openToStream(QFile *handler);
	void freadError(const QString &filename);
	static QStringList trimCrap(QStringList values);

	ProteinDB &proteins;
};

#endif // STORAGE_H
