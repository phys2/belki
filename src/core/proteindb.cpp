#include "proteindb.h"
#include "../compute/annotations.h"
#include "../compute/colors.h"

#include <QTextStream>
#include <QRegularExpression>

ProteinDB::ProteinDB(QObject *parent)
    : QObject(parent)
{
	colorset = Palette::iwanthue20;
	groupColorset = colorset;
	for (auto &c : groupColorset)
		c = c.lighter(130); // 30 % lighter
}

void ProteinDB::init(std::unique_ptr<ProteinRegister> payload)
{
	QWriteLocker l(&data.l);
	if (!data.proteins.empty())
		throw std::runtime_error("ProteinDB::init() called on non-empty object");

	data.proteins = payload->proteins; // *don't move*, we need copy for signal
	data.index = std::move(payload->index);
	data.markers = std::move(payload->markers);
	data.structures = std::move(payload->structures);

	for (auto &[k, _] : data.structures)
		data.nextStructureId = std::max(data.nextStructureId, k + 1);

	/* keep metadata for signals */
	std::vector<ProteinId> markers(data.markers.cbegin(), data.markers.cend());
	std::vector<std::pair<unsigned, QString>> structures; // [id, name]
	for (auto &[k, v] : data.structures) {
		auto nameOf = [] (auto *s) { return (s ? s->meta.name : ""); };
		auto a = std::get_if<Annotations>(&v);
		auto b = std::get_if<HrClustering>(&v);
		structures.push_back(std::make_pair(k, nameOf(a) + nameOf(b)));
	}
	l.unlock();

	/* emit signals – w/o lock */
	for (unsigned i = 0; i < payload->proteins.size(); ++i)
		emit proteinAdded(i, payload->proteins[i]);
	emit markersToggled(markers, true);
	for (auto &[id, name] : structures)
		emit structureAvailable(id, name, false);
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
	p.species = (parts.size() > 1 ? parts.back() : "MOUSE"); // wild guess, good Uniprot coverage
	p.color = colorFor(p);

	/* insert */
	id = data.proteins.size();
	data.index[p.name] = id;
	data.proteins.push_back(p); // don't move, needed in signal

	l.unlock();
	emit proteinAdded(id, p);
	return id;
}

bool ProteinDB::addDescription(const QString& name, const QString& desc)
{
	QWriteLocker l(&data.l);
	try {
		auto id = data.find(name);
		data.proteins[id].description = desc;
		l.unlock();
		emit proteinChanged(id);
		return true;
	} catch (std::out_of_range&) {
		return false;
	}
}

bool ProteinDB::readDescriptions(QTextStream in)
{
	auto header = in.readLine().split("\t");
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() != 2 || !header[0].contains(re)) {
		emit message({"Could not parse file!",
		              "The first column must contain protein names, second descriptions."});
		return false;
	}

	/* ensure we have data to annotate */
	if (peek()->proteins.empty()) {
		emit message({"Please load proteins first!"});
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
	auto [_, isnew] = data.markers.insert(id);
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

void ProteinDB::toggleMarkers(const std::vector<ProteinId> &ids, bool on)
{
	std::vector<ProteinId> affected;
	data.l.lockForWrite();
	if (on) {
		for (const auto id : ids) {
			auto [_, change] = data.markers.insert(id);
			if (change)
				affected.push_back(id);
		}
	} else {
		for (const auto id : ids) {
			auto change = data.markers.erase(id);
			if (change)
				affected.push_back(id);
		}
	}
	data.l.unlock();
	if (!affected.empty())
		emit markersToggled(affected, on);
}

size_t ProteinDB::importMarkers(const std::vector<QString> &names)
{
	QWriteLocker l(&data.l);
	std::vector<ProteinId> wanted;
	for (const auto &name : names) {
		try {
			wanted.push_back(data.find(name));
		} catch (std::out_of_range&) {}
	}

	if (wanted.size() > 500) {
		emit message({"Too many protein names in marker file.",
		              QString("The maximum number of markers is %2, but the file contains %1 "
		              "proteins from the project.").arg(wanted.size()).arg(500)});
		return 0;
	}

	if (wanted.empty()) {
		emit message({"No proteins from the project found in marker file.", {}, GuiMessage::INFO});
		return 0;
	}

	std::vector<ProteinId> affected;
	for (const auto id : wanted) {
		auto [_, isnew] = data.markers.insert(id);
		if (isnew)
			affected.push_back(id);
	}
	l.unlock();

	if (!affected.empty())
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

	auto name = a->meta.name;

	data.l.lockForWrite();
	auto id = data.nextStructureId++; // pick an id that was not in use before
	a->meta.id = id;
	data.structures[id] = std::move(*a);
	data.l.unlock();

	emit structureAvailable(id, name, select);
}

void ProteinDB::addHierarchy(std::unique_ptr<HrClustering> h, bool select)
{
	auto name = h->meta.name;

	data.l.lockForWrite();
	auto id = data.nextStructureId++; // pick an id that was not in use before
	h->meta.id = id;
	data.structures[id] = std::move(*h);
	data.l.unlock();

	emit structureAvailable(id, name, select);
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
