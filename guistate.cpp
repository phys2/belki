#include "guistate.h"
#include "widgets/mainwindow.h"

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QTimer>

GuiState::GuiState(DataHub &hub) : hub(hub)
{
	setupMarkerControl();

	connect(&hub.proteins, &ProteinDB::proteinAdded, this, &GuiState::addProtein);
	connect(&hub.proteins, &ProteinDB::markersToggled,
	        this, [this] (auto ids, bool present) {
		auto state = present ? Qt::Checked : Qt::Unchecked;
		for (auto id : ids)
			this->markerControl.items.at(id)->setCheckState(state);
	});
}

unsigned GuiState::addWindow()
{
	auto [it,_] = windows.try_emplace(nextId++, new MainWindow(hub));
	auto target = it->second;
	target->setMarkerControlModel(markerControl.model);

	connect(target, &MainWindow::newWindowRequested,
	        this, &GuiState::addWindow);
	connect(target, &MainWindow::closeWindowRequested, this,
	        [this,id=it->first] { removeWindow(id); });
	connect(target, &MainWindow::markerFlipped, this, &GuiState::flipMarker);
	connect(target, &MainWindow::markerToggled, this, &GuiState::toggleMarker);

	target->show();
	return it->first;
}

void GuiState::removeWindow(unsigned id)
{
	auto w = windows.at(id);
	w->deleteLater(); // do not delete a window within its close event
	windows.erase(id);
	if (windows.empty())
		QApplication::quit();
}

void GuiState::addProtein(ProteinId id, const Protein &protein)
{
	/* setup new item */
	auto item = new QStandardItem;
	item->setText(protein.name);
	item->setData(id);
	item->setCheckable(true);
	item->setCheckState(Qt::Unchecked);

	/* add item to model */
	markerControl.model->appendRow(item);
	markerControl.items[id] = item;

	/* ensure items are sorted in the end, but defer sorting */
	markerControl.dirty = true;
	QTimer::singleShot(0, this, &GuiState::sortMarkerModel);
}

void GuiState::flipMarker(QModelIndex i)
{
	if (!i.isValid())
		return; // didn't click on a row, e.g. clicked on a checkmark
	auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
	auto item = markerControl.model->itemFromIndex(
	                proxy ? proxy->mapToSource(i) : i);
	if (!item->isEnabled())
		return;
	item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void GuiState::toggleMarker(ProteinId id, bool present)
{
	markerControl.items.at(id)->setCheckState(present ? Qt::Checked : Qt::Unchecked);
}

void GuiState::setupMarkerControl()
{
	markerControl.model = new QStandardItemModel(this);
	connect(markerControl.model, &QStandardItemModel::itemChanged,
	        [this] (QStandardItem *i) {
		// TODO this is called also when items are enabled/disabled
		// and that happens for quite many proteins at once :-/
		auto id = ProteinId(i->data().toInt());
		bool wanted = i->checkState() == Qt::Checked;
		//if (hub.proteins.peek()->markers.count(id) == wanted)
		//	return;
		if (wanted)
			hub.proteins.addMarker(id);
		else
			hub.proteins.removeMarker(id);
	});
}

void GuiState::sortMarkerModel()
{
	if (!markerControl.dirty) // already in good state
		return;
	markerControl.model->sort(0);
	markerControl.dirty = false;
}

