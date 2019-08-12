#include "proteindb.h"
#include "compute/annotations.h"

#include <QTextStream>
#include <QRegularExpression>

ProteinDB::ProteinDB(QObject *parent)
    : QObject(parent)
{
	qRegisterMetaType<ProteinId>("ProteinId"); // needed for typedefs
}

ProteinId ProteinDB::add(const QString &fullname)
{
	ProteinId id;
	QWriteLocker l(&data.l);

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

	l.unlock();
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

bool ProteinDB::readDescriptions(QTextStream &in)
{
	auto header = in.readLine().split("\t");
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() != 2 || !header[0].contains(re)) {
		emit ioError("Could not parse file!<p>The first column must contain protein names, second descriptions.</p>");
		return false;
	}

	/* ensure we have data to annotate */
	if (peek()->proteins.empty()) {
		emit ioError("Please load proteins first!");
		return false;
	}

	/* fill-in descriptions */
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() < 2)
			continue;

		// note: this locks everytime. We don't care right now…
		addDescription(line[0], line[1]);
	}

	return true;
}

bool ProteinDB::addMarker(ProteinId id)
{
	data.l.lockForWrite();
	auto [at, isnew] = data.markers.insert(id);
	data.l.unlock();
	if (isnew)
		emit markersToggled({id}, true);
	return isnew;
}

bool ProteinDB::removeMarker(ProteinId id)
{
	data.l.lockForWrite();
	bool affected = data.markers.erase(id);
	data.l.unlock();
	if (affected)
		emit markersToggled({id}, false);
	return affected;
}

size_t ProteinDB::importMarkers(const std::vector<QString> &names)
{
	std::vector<ProteinId> wanted;
	for (const auto &name : names)
		wanted.push_back(add(name));

	std::vector<ProteinId> affected;
	data.l.lockForWrite();
	for (const auto &id : wanted) {
		auto [at, isnew] = data.markers.insert(id);
		if (isnew)
			affected.push_back(id);
	}
	data.l.unlock();

	emit markersToggled(affected, true);
	return affected.size();
}

void ProteinDB::clearMarkers()
{
	data.l.lockForWrite();
	std::vector<ProteinId> affected(data.markers.begin(), data.markers.end());
	data.markers.clear();
	data.l.unlock();
	emit markersToggled(affected, false);
}

void ProteinDB::addAnnotations(std::unique_ptr<Annotations> a, bool select, bool pristine)
{
	if (!pristine) {
		annotations::order(*a, false);
		annotations::color(*a, colorset);
	}

	auto name = a->name;

	data.l.lockForWrite();
	auto id = data.nextStructureId++; // pick an id that was not in use before
	data.structures[id] = std::move(*a);
	data.l.unlock();

	emit structureAvailable(id, name, select);
}

void ProteinDB::addHierarchy(std::unique_ptr<HrClustering> h, bool select)
{
	auto name = h->name;

	data.l.lockForWrite();
	auto id = data.nextStructureId++; // pick an id that was not in use before
	data.structures[id] = std::move(*h);
	data.l.unlock();

	emit structureAvailable(id, name, select);
}

void ProteinDB::updateColorset(const QVector<QColor> &colors)
{
	QWriteLocker _(&data.l);

	colorset = colors;
	for (auto &p : data.proteins)
		p.color = colorFor(p);
}

QColor ProteinDB::colorFor(const Protein &subject)
{
	return colorset.at((int)(qHash(subject.name) % (unsigned)colorset.size()));
}

ProteinId ProteinDB::Public::find(const QString &name) const
{
	return index.at(name.split('_').front());
}

bool ProteinDB::Public::isHierarchy(unsigned id) const
{
	return std::holds_alternative<HrClustering>(structures.at((unsigned)id));
}
