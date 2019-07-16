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
	void ioError(const QString &message);
	void newDisplay(const QString &name, bool loaded=false);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void updateColorset(QVector<QColor> colors);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename, std::shared_ptr<Dataset const> data);

protected:
	Features::Ptr readSimpleSource(QTextStream &in);
	void finalizeRead(Features &data, bool normalize);

	void storeDisplay(const Dataset &data, const QString& name);

	void close(bool save = false);
	void freadError(const QString &filename);
	static QStringList trimCrap(QStringList values);

	struct : public RWLockable {
		QString sourcename; // a file can have several source data in general, we only support/select one right now
		std::unique_ptr<qzip::Zip> container;
	} d;

	ProteinDB &proteins;
	QVector<QColor> colorset;
};

#endif // STORAGE_H
