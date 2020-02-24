#ifndef PROTEINDB_H
#define PROTEINDB_H

#include "model.h"
#include "utils.h"

#include <QObject>
#include <QVector>

#include <vector>
#include <unordered_map>
#include <set>
#include <memory>

class QTextStream;

class ProteinDB : public QObject
{
	Q_OBJECT

public:
	struct Public : RWLockable {
		// helper for finding proteins, name may contain species, throws
		ProteinId find(const QString &name) const;
		// helper for annotations type
		bool isHierarchy(unsigned id) const;

		std::vector<Protein> proteins;
		std::unordered_map<QString, ProteinId> index;

		// TODO: sort set by prot. name
		std::set<ProteinId> markers;

		std::unordered_map<unsigned, Structure> structures;
		unsigned nextStructureId = 1;
	};

	using View = ::View<Public>;

	explicit ProteinDB(QObject *parent = nullptr);

	const QVector<QColor>& groupColors() { return groupColorset; }
	View peek() { return View(data); }

	ProteinId add(const QString& fullname);
	bool addDescription(const QString& name, const QString& desc);
	bool readDescriptions(QTextStream tsv);

	bool addMarker(ProteinId id);
	bool removeMarker(ProteinId id);
	size_t importMarkers(const std::vector<QString> &names);
	void clearMarkers();

	void addAnnotations(std::unique_ptr<Annotations> data, bool select, bool pristine = false);
	void addHierarchy(std::unique_ptr<HrClustering> data, bool select);

signals:
	void ioError(const QString &message, MessageType type = MessageType::CRITICAL);
	void proteinAdded(ProteinId id, const Protein &protein);
	void proteinChanged(ProteinId id);
	void markersToggled(const std::vector<ProteinId> &id, bool present);
	void structureAvailable(unsigned id, QString name, bool select);

protected:
	bool valid(ProteinId id);
	QColor colorFor(const Protein &subject);

	Public data;

	QVector<QColor> colorset, groupColorset;
};

#endif // PROTEINDB_H
