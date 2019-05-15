#ifndef PROTEINDB_H
#define PROTEINDB_H

#include "utils.h"

#include <QObject>
#include <QString>
#include <QColor>
#include <QVector>

#include <vector>
#include <unordered_map>
#include <set>

using ProteinId = unsigned; // for semantic distinction

class ProteinDB : public QObject
{
	Q_OBJECT

public:
	struct Protein {
		// first part of protein name, used as identifier
		QString name;
		// last part of protein name
		QString species;
		// description, if any
		QString description;
		// random or user-set color
		QColor color;
	};

	using ProteinIndex = std::unordered_map<QString, ProteinId>;

	struct Public : RWLockable {
		// helper for finding proteins, name may contain species, throws
		ProteinId find(const QString &name) const;

		std::vector<Protein> proteins;
		ProteinIndex index;

		// TODO: sort set by prot. name
		std::set<ProteinId> markers;
	};

	using View = ::View<Public>;

	ProteinDB();

	View peek() { return View(data); }

	ProteinId add(const QString& fullname);
	bool addDescription(const QString& name, const QString& desc);

	bool addMarker(ProteinId id);
	bool removeMarker(ProteinId id);
	void clearMarkers();

	void updateColorset(const QVector<QColor>& colors);

signals:
	void proteinAdded(ProteinId id);
	void proteinChanged(ProteinId id);
	void markerToggled(ProteinId id, bool present);

protected:
	bool valid(ProteinId id);
	QColor colorFor(const Protein &subject);

	Public data;

	QVector<QColor> colorset;
};

#endif // PROTEINDB_H
