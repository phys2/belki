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
class QFileDevice;
class QIODevice;
class QTextStream;
class QJsonDocument;
class QCborValue;

class Storage : public QObject
{
	Q_OBJECT
public:
	struct ReadConfig {
		QString featureColName = "Dist";
		bool normalize = false;
	};

	Storage(ProteinDB &proteins, QObject *parent = nullptr);

	std::vector<std::shared_ptr<Dataset>> openProject(const QString &filename);
	bool saveProject(const QString &filename, std::vector<std::shared_ptr<const Dataset>> snapshot);

	Features::Ptr openDataset(const QString &filename, const ReadConfig &config);

signals: // IMPORTANT: always provide target object pointer for thread-affinity
	void nameChanged(const QString &name, const QString &path);
	void message(const GuiMessage &message);

public slots:
	void importMarkers(const QString &filename);
	void exportMarkers(const QString &filename);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename, const Annotations &source);

protected:
	void updateFilename(const QString &filename);

	// see storage/parse_dataset.cpp
	Features::Ptr readSource(QTextStream in, const ReadConfig &config);
	Features::Ptr readSimpleSource(QTextStream &in, bool normalize);
	void finalizeRead(Features &data, bool normalize);

	// see storage/serialize.cpp
	void writeProject(QIODevice *target, std::vector<std::shared_ptr<const Dataset>> snapshot);
	QCborValue serializeDataset(std::shared_ptr<const Dataset> src);
	QCborValue serializeProteinDB();
	QCborValue serializeStructure(const Structure &src);

	// see storage/deserialize.cpp
	std::vector<std::shared_ptr<Dataset>> readProject(const QString &filename);
	template<int VER>
	void deserializeProteinDB(const QCborMap &proteindb);
	template<int VER>
	Structure deserializeStructure(const QCborMap &structure, unsigned id);
	template<int VER>
	std::shared_ptr<Dataset> deserializeDataset(const QCborMap &dataset);
	template<int VER>
	std::vector<std::shared_ptr<Dataset>> deserializeProject(const QCborMap &top);

	// TODO dead code right now
	void storeDisplay(const Representations::Pointset &disp, const QString& name);
	void readDisplay(const QString& name, QTextStream &in);

	QTextStream openToStream(QFileDevice *handler);
	void fileError(QFileDevice *f, bool write = false);
	static QStringList trimCrap(QStringList values);

	ProteinDB &proteins;
};

#endif // STORAGE_H
