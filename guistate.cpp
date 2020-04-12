#include "guistate.h"
#include "datahub.h"
#include "widgets/mainwindow.h"
#include "fileio.h"
#include "jobregistry.h"

#include <QAbstractProxyModel>
#include <QTimer>
#include <QEvent>
#include <QMenu>
#include <QPushButton>
#include <QMessageBox>
#include <QWidgetAction>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>

GuiState::GuiState(DataHub &hub)
    : hub(hub), proteins(hub.proteins),
      io(std::make_unique<FileIO>())
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
	connect(&markers.model, &QStandardItemModel::itemChanged,
	        this, &GuiState::handleMarkerChange);
	connect(io.get(), &FileIO::message, this, &GuiState::displayMessage);

	/* notifications from protein db and data hub */
	connect(&hub, &DataHub::message, this, &GuiState::displayMessage);
	connect(&proteins, &ProteinDB::proteinAdded, this, &GuiState::addProtein);
	connect(&proteins, &ProteinDB::markersToggled,
	        this, [this] (auto ids, bool present) {
		auto state = present ? Qt::Checked : Qt::Unchecked;
		for (auto id : ids)
			this->markers.items.at(id)->setCheckState(state);
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
	connect(&hub, &DataHub::datasetRemoved, this, &GuiState::removeDataset);
}

GuiState::~GuiState()
{
	shutdown(false);
}

std::unique_ptr<QMenu> GuiState::proteinMenu(ProteinId id)
{
	auto p = proteins.peek();
	auto name = p->proteins[id].name;
	auto ret = std::make_unique<QMenu>(name);
	// TODO icon based on color
	auto label = new QLabel(name);
	QString style{"QLabel {background-color: %2; color: white; font-weight: bold}"};
	label->setStyleSheet(style.arg(p->proteins[id].color.name()));
	label->setAlignment(Qt::AlignCenter);
	label->setMargin(2);
	auto t = new QWidgetAction(ret.get());
	t->setDefaultWidget(label);
	ret->addAction(t);

	if (p->isMarker(id)) {
		ret->addAction(QIcon::fromTheme("list-remove"), "Remove from markers", [this,id] {
			proteins.removeMarker(id);
		});
	} else {
		ret->addAction(QIcon::fromTheme("list-add"), "Add to markers", [this,id] {
			proteins.addMarker(id);
		});
	}
	ret->addSeparator();
	ret->addAction(QIcon::fromTheme("edit-copy"), "Copy name to clipboard", [name] {
		QGuiApplication::clipboard()->setText(name);
	});
	auto url = QString{"https://uniprot.org/uniprot/%1_%2"}
	           .arg(p->proteins[id].name, p->proteins[id].species);
	ret->addAction(QIcon::fromTheme("globe"), "Lookup in Uniprot", [url] {
		QDesktopServices::openUrl(url);
	});
	return ret;
}

void GuiState::addWindow()
{
	auto [it,_] = windows.try_emplace(nextId++, new MainWindow(*this));
	auto target = it->second;
	target->installEventFilter(this); // for focus tracking
	target->setDatasetControlModel(&datasets.model);
	target->setMarkerControlModel(&markers.model);
	target->setStructureControlModel(&structureModel);

	connect(target, &MainWindow::message, this,
	        [this,target] (const auto &message) { displayMessageAt(message, target); });
	connect(target, &MainWindow::newWindowRequested, this, &GuiState::addWindow);
	connect(target, &MainWindow::closeWindowRequested, this,
	        [this,id=it->first] { removeWindow(id); });
	connect(target, &MainWindow::closeProjectRequested, this, [this] { GuiState::shutdown(); });
	connect(target, &MainWindow::openProjectRequested, this, &GuiState::openProject);
	connect(target, &MainWindow::quitApplicationRequested, this, &GuiState::quitRequested);
	connect(target, &MainWindow::markerFlipped, this, &GuiState::flipMarker);

	connect(&hub, &DataHub::projectNameChanged, target, &MainWindow::setName);
	connect(&hub, &DataHub::datasetRemoved, target, &MainWindow::removeDataset);

	// pick latest dataset as a starting point
	auto datasets = hub.datasets();
	if (!datasets.empty())
		target->setDataset(hub.datasets().rbegin()->second);

	target->show();
}

void GuiState::removeWindow(unsigned id, bool withPrompt)
{
	/* prompt first on last windows*/
	if (withPrompt && windows.size() < 2 && !promptOnClose())
		return;

	auto w = windows.at(id);
	/* explicitely hide first, for two reasons:
	 * - don't keep windows lingering, e.g. while new modal dialogs pop up
	 * - we observed a strange spurious access violation in Chart::hibernate(),
	 *   which indicates that the window was deleted _after_ close() below.
	 *   Strange as we use QTimer to avoid exactly that… */
	w->hide();
	w->deleteLater(); // do not delete a window within its close event
	windows.erase(id);
	if (windows.empty())
		QTimer::singleShot(0, [this] {emit closed();}); // delete us later
}

void GuiState::openProject(const QString &filename)
{
	bool proceed = true;
	QMessageBox dialog(focused());
	auto name = hub.projectMeta().name;
	dialog.setText("Close current project?");
	dialog.setInformativeText(QString{
	                              "The project to be loaded will be opened in a new window."
	                              "<br>Would you like to close %1?"
	                          }.arg(name.isEmpty() ? "the current project" : name));
	auto keepOpen = dialog.addButton("Keep open", QMessageBox::NoRole);
	std::map<QAbstractButton*, std::function<void()>> actions = {
	  {keepOpen, [&] {}},
	  {dialog.addButton("Close project", QMessageBox::DestructiveRole),
       [&] { shutdown(false); }},
	  {dialog.addButton(QMessageBox::Cancel), [&] { proceed = false; }},
	  {nullptr, [&] { proceed = false; }},
	};
	if (!name.isEmpty()) {
		actions.insert_or_assign(dialog.addButton("Save && Close", QMessageBox::YesRole),
		  [&] { hub.saveProject(); shutdown(false); });
	}
	dialog.setDefaultButton(keepOpen);
	dialog.exec();
	actions.at(dialog.clickedButton())();

	if (proceed)
		emit instanceRequested(filename);
}

void GuiState::addDataset(Dataset::Ptr dataset)
{
	auto conf = dataset->config();
	auto parent = (conf.parent ? datasets.items.at(conf.parent)
	                           : datasets.model.invisibleRootItem()); // top level
	auto item = new QStandardItem(conf.name);
	item->setData(dataset->id(), Qt::UserRole);
	item->setData(QVariant::fromValue(dataset), Qt::UserRole + 1);

	parent->appendRow(item);
	datasets.items[conf.id] = item;

	// auto-select
	auto target = focused();
	if (target)
		target->setDataset(dataset);
}

void GuiState::removeDataset(unsigned id)
{
	try {
		auto item = datasets.items.at(id);
		// removing the row deletes the QStandardItem
		datasets.model.removeRow(item->row(),
		                               (item->parent() ? item->parent()->index() : QModelIndex{}));
		datasets.items.erase(id);
	} catch (std::out_of_range&) {}
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
	markers.model.appendRow(item);
	markers.items[id] = item;

	/* ensure items are sorted in the end, but defer sorting */
	markers.dirty = true;
	QTimer::singleShot(0, this, &GuiState::sortMarkerModel);
}

void GuiState::flipMarker(QModelIndex i)
{
	if (!i.isValid())
		return; // didn't click on a row, e.g. clicked on a checkmark
	while (auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model()))
		i = proxy->mapToSource(i);
	auto item = markers.model.itemFromIndex(i);
	if (!item->isEnabled())
		return;
	item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void GuiState::handleMarkerChange(QStandardItem *item)
{
	auto id = ProteinId(item->data().toInt());
	bool wanted = item->checkState() == Qt::Checked;
	/* We are called on check state change, but also other item changes,
	   e.g. quite many items get enabled/disabled regularly. */
	if (proteins.peek()->isMarker(id) == wanted)
		return;
	if (wanted)
		proteins.addMarker(id);
	else
		proteins.removeMarker(id);
}

void GuiState::displayMessage(const GuiMessage &message)
{
	displayMessageAt(message, focused());
}

void GuiState::displayMessageAt(const GuiMessage &message, QWidget *parent)
{
	QMessageBox dialog(parent);
	dialog.setText(message.text);
	dialog.setInformativeText(message.informativeText);
	switch (message.type) {
	case GuiMessage::INFO:     dialog.setIcon(QMessageBox::Information); break;
	case GuiMessage::WARNING:  dialog.setIcon(QMessageBox::Warning); break;
	case GuiMessage::CRITICAL: dialog.setIcon(QMessageBox::Critical); break;
	};
	dialog.exec();
}

bool GuiState::promptOnClose(QWidget *parent)
{
	if (proteins.peek()->proteins.empty())
		return true; // no need to ask, empty project

	/* Lazy way: If no filename set, only provide Ok/Cancel, otherwise Save/Discard/Cancel */
	QMessageBox dialog(parent ? parent : focused());
	auto name = hub.projectMeta().name;
	if (name.isEmpty()) {
		dialog.setText("Close project?");
		dialog.setInformativeText("The project has not been saved.");
		dialog.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
	} else {
		dialog.setText(QString("Close project %1?").arg(name));
		dialog.setInformativeText("The project might have unsaved changes."
		                          "<br>Would you like to save it first?");
		dialog.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	}
	dialog.setDefaultButton(QMessageBox::Cancel);
	dialog.setEscapeButton(QMessageBox::Cancel);
	int ret = dialog.exec();
	if (ret == QMessageBox::Save)
		hub.saveProject();
	return (ret != QMessageBox::Cancel);
}

bool GuiState::shutdown(bool withPrompt)
{
	/* prompt first */
	if (withPrompt && !promptOnClose())
		return false;

	/* close all windows, which will lead to our demise */
	std::vector<unsigned> cache; // cache ids to avoid invalid iterators
	for (auto &[k, _] : windows)
		cache.push_back(k);
	for (auto i : cache)
		removeWindow(i, false);
	return true;
}

void GuiState::sortMarkerModel()
{
	if (!markers.dirty) // already in good state
		return;
	markers.model.sort(0);
	markers.dirty = false;
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
