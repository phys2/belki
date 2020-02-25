#include "storage.h"
#include "dataset.h"

#include <QCborValue>
#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QFile>

template<int VER>
std::vector<std::shared_ptr<Dataset>> deserializeProject(const QCborMap &top);

template<>
std::vector<std::shared_ptr<Dataset>> deserializeProject<1>(const QCborMap &top) {
	// TODO: From here on, we expect a valid layout. Add checks where needed
	auto proteindb = top.value("proteindb").toMap();
	// TODO initialize/overwrite ProteinDB
	auto datasets = top.value("proteindb").toArray();
	// TODO read one by one
	return {};
}

std::vector<std::shared_ptr<Dataset>> Storage::openProject(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		ioError(QString("Could not open file %1!").arg(filename));
		return {};
	}
	QCborStreamReader r(&f);
	/* We expect a map with version etc. on top level */
	if (r.isTag() && r.toTag() == QCborKnownTags::Signature)
		r.next();
	auto top = QCborValue::fromCbor(r).toMap();
	if (r.lastError() != QCborError::NoError) {
		ioError(QString("Error reading file:<p>%1</p>").arg(r.lastError().toString()));
		return {};
	}
	auto version = top.value("Belki File Version");
	if (not version.isInteger()) {
		ioError("Invalid file, could not read version");
		return {};
	}

	if (version.toInteger(0) == 1)
		return deserializeProject<1>(top);

	ioError(QString("File version %d not supported.<p>Please upgrade Belki.</p>").arg(version.toInteger()));
	return {};
}
