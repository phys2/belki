#include "proteindb.h"
#include <iostream>

ProteinDB::ProteinDB()
{
	qRegisterMetaType<ProteinId>("ProteinId"); // needed for typedefs
}

ProteinId ProteinDB::add(const QString &fullname)
{
	ProteinId id;
	{
		auto _ = data.wlock();

		/* check presence first */
		try {
			return data.find(fullname);
		} catch (std::out_of_range&) {}

		/* setup protein */
		Protein p;
		auto parts = fullname.split("_");
		p.name = parts.front();
		p.species = (parts.size() > 1 ? parts.back() : "RAT"); // wild guess
		p.color = colorFor(p);

		/* insert */
		id = data.proteins.size();
		data.index[p.name] = id;
		data.proteins.push_back(std::move(p)); // do this last (invalidates p)
	}

	emit proteinAdded(id);
	return id;
}

bool ProteinDB::addDescription(const QString& name, const QString& desc)
{
	try {
		data.l.lockForWrite();
		auto id = data.find(name);
		data.proteins[id].description = desc;
		data.l.unlock();
		emit proteinChanged(id);
		return true;
	} catch (std::out_of_range&) {
		return false;
	}
}

bool ProteinDB::addMarker(ProteinId id)
{
	data.l.lockForWrite();
	auto [at, isnew] = data.markers.insert(id);
	data.l.unlock();
	if (isnew)
		emit markerToggled(id, true);
	return isnew;
}

bool ProteinDB::removeMarker(ProteinId id)
{
	data.l.lockForWrite();
	bool affected = data.markers.erase(id);
	data.l.unlock();
	if (affected)
		emit markerToggled(id, false);
	return affected;
}

void ProteinDB::clearMarkers()
{
	data.l.lockForWrite();
	std::set<ProteinId> markers;
	data.markers.swap(markers);
	data.l.unlock();
	for (auto &id : markers)
		emit markerToggled(id, false);
}

void ProteinDB::updateColorset(const QVector<QColor> &colors)
{
	auto _ = data.wlock();

	colorset = colors;
	for (auto &p : data.proteins)
		p.color = colorFor(p);
}

QColor ProteinDB::colorFor(const Protein &subject)
{
	return colorset[(int)qHash(subject.name) % colorset.size()];
}

ProteinId ProteinDB::Public::find(const QString &name) const {
	return index.at(name.split('_').front());
}
