#include "guistate.h"
#include "windowstate.h"
#include "datahub.h"
#include "widgets/mainwindow.h"

#include <QAbstractProxyModel>
#include <QTimer>
#include <QEvent>
#include <QMenu>
#include <QWidgetAction>
#include <QDesktopServices>

GuiState::GuiState(DataHub &hub)
    : hub(hub), proteins(hub.proteins), store(hub.store)
{
	auto addStructureItem = [this] (QString name, QString icon, int id) {
		auto item = new QStandardItem(name);
		if (!icon.isEmpty())
			item->setIcon(QIcon(icon));
		item->setData(id, Qt::UserRole);
		structureModel.appendRow(item);
	};

	/* prepare default structure items */
	addStructureItem("None", {}, 0);
	addStructureItem("Adaptive Mean Shift", ":/icons/type-meanshift.svg", -1);

	/* internal wiring */
	connect(&markerControl.model, &QStandardItemModel::itemChanged,
	        this, &GuiState::handleMarkerChange);

	/* notifications from Protein db */
	connect(&hub, &DataHub::ioError, this, &GuiState::displayMessage);
	connect(&proteins, &ProteinDB::proteinAdded, this, &GuiState::addProtein);
	connect(&proteins, &ProteinDB::markersToggled,
	        this, [this] (auto ids, bool present) {
		auto state = present ? Qt::Checked : Qt::Unchecked;
		for (auto id : ids)
			this->markerControl.items.at(id)->setCheckState(state);
	});
	connect(&proteins, &ProteinDB::structureAvailable, this,
	        [this,addStructureItem] (unsigned id, QString name, bool select) {
		auto icon = (proteins.peek()->isHierarchy(id) ? "hierarchy" : "annotations");
		addStructureItem(name, QString(":/icons/type-%1.svg").arg(icon), (int)id);
		if (select) {
			auto target = focused();
			if (target)
				target->selectStructure((int)id);
		}
	});
	connect(&hub, &DataHub::newDataset, this, &GuiState::addDataset);
}

std::unique_ptr<QMenu> GuiState::proteinMenu(ProteinId id)
{
	auto p = proteins.peek();
	auto title = p->proteins[id].name;
	auto ret = std::make_unique<QMenu>(title);
	// TODO icon based on color
	auto label = new QLabel(title);
	QString style{"QLabel {background-color: %2; color: white; font-weight: bold}"};
	label->setStyleSheet(style.arg(p->proteins[id].color.name()));
	label->setAlignment(Qt::AlignCenter);
	label->setMargin(2);
	auto t = new QWidgetAction(ret.get());
	t->setDefaultWidget(label);
	ret->addAction(t);

	if (p->markers.count(id)) {
		ret->addAction(QIcon::fromTheme("list-remove"), "Remove from markers", [this,id] {
			proteins.removeMarker(id);
		});
	} else {
		ret->addAction(QIcon::fromTheme("list-add"), "Add to markers", [this,id] {
			proteins.addMarker(id);
		});
	}
	ret->addSeparator();
	auto url = QString{"https://uniprot.org/uniprot/%1_%2"}
	           .arg(p->proteins[id].name, p->proteins[id].species);
	ret->addAction(QIcon::fromTheme("globe"), "Lookup in Uniprot", [url] {
		QDesktopServices::openUrl(url);
	});
	return ret;
}

unsigned GuiState::addWindow()
{
	auto state = std::make_shared<WindowState>(*this);
	auto [it,_] = windows.try_emplace(nextId++, new MainWindow(state));
	auto target = it->second;
	target->installEventFilter(this); // for focus tracking
	target->setDatasetControlModel(&datasetControl.model);
	target->setMarkerControlModel(&markerControl.model);
	target->setStructureControlModel(&structureModel);

	connect(target, &MainWindow::newWindowRequested,
	        this, &GuiState::addWindow);
	connect(target, &MainWindow::closeWindowRequested, this,
	        [this,id=it->first] { removeWindow(id); });
	connect(target, &MainWindow::markerFlipped, this, &GuiState::flipMarker);
	connect(target, &MainWindow::markerToggled, this, &GuiState::toggleMarker);

	// pick latest dataset as a starting point
	auto datasets = hub.datasets();
	if (!datasets.empty())
		target->setDataset(hub.datasets().rbegin()->second);

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

void GuiState::addDataset(Dataset::Ptr dataset)
{
	auto conf = dataset->config();
	auto parent = (conf.parent ? datasetControl.items.at(conf.parent)
	                           : datasetControl.model.invisibleRootItem()); // top level
	auto item = new QStandardItem(conf.name);
	item->setData(dataset->id(), Qt::UserRole);
	item->setData(QVariant::fromValue(dataset), Qt::UserRole + 1);
	parent->appendRow(item);
	datasetControl.items[conf.id] = item;

	// auto-select
	auto target = focused();
	if (target)
		target->setDataset(dataset);
}

void GuiState::removeDataset(unsigned id)
{
	auto item = datasetControl.items.at(id);
	delete item; // TODO: does it deregister from model? test!
	datasetControl.items.erase(id);
	// TODO: select another one?
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
	markerControl.model.appendRow(item);
	markerControl.items[id] = item;

	/* ensure items are sorted in the end, but defer sorting */
	markerControl.dirty = true;
	QTimer::singleShot(0, this, &GuiState::sortMarkerModel);
}

void GuiState::flipMarker(QModelIndex i)
{
	if (!i.isValid())
		return; // didn't click on a row, e.g. clicked on a checkmark
	while (auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model()))
		i = proxy->mapToSource(i);
	auto item = markerControl.model.itemFromIndex(i);
	if (!item->isEnabled())
		return;
	item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void GuiState::toggleMarker(ProteinId id, bool present)
{
	markerControl.items.at(id)->setCheckState(present ? Qt::Checked : Qt::Unchecked);
}

void GuiState::handleMarkerChange(QStandardItem *item)
{
	auto id = ProteinId(item->data().toInt());
	bool wanted = item->checkState() == Qt::Checked;
	/* We are called on check state change, but also other item changes,
	   e.g. quite many items get enabled/disabled regularly. */
	if (proteins.peek()->markers.count(id) == wanted)
		return;
	if (wanted)
		proteins.addMarker(id);
	else
		proteins.removeMarker(id);
}

void GuiState::displayMessage(const QString &message, MessageType type)
{
	auto target = focused();
	if (target)
		target->displayMessage(message, type);
}

void GuiState::sortMarkerModel()
{
	if (!markerControl.dirty) // already in good state
		return;
	markerControl.model.sort(0);
	markerControl.dirty = false;
}

MainWindow *GuiState::focused()
{
	if (windows.empty())
		return nullptr;
	for (auto &[k, v] : windows) {
		if (v->hasFocus())
			return v;
	}
	if (lastFocused)
		return lastFocused;
	return windows.rbegin()->second; // default to latest
}

bool GuiState::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::Enter) {
		auto cand = qobject_cast<MainWindow*>(watched);
		if (cand)
			lastFocused = cand;
	}
	return false; // not filtered, pass through
}
