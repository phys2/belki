#include "mainwindow.h"
#include "centralhub.h"
#include "dataset.h"
#include "storage.h"
#include "widgets/profilechart.h"
#include "widgets/profilewindow.h"
#include "widgets/spawndialog.h"

#include <QTreeWidget>
#include <QFileInfo>
#include <QDir>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>

#include <iostream>

constexpr auto hierarchyPostfix = " (Hierarchy)";

MainWindow::MainWindow(CentralHub &hub) :
    hub(hub),
    io(new FileIO(this))
{
	setupUi(this);
	setupToolbar();

	auto renderSlot = [this] (auto r, auto d) { io->renderToFile(r, {title, d}); };

	/* Views in tabs */
	views = {dimredTab, scatterTab, heatmapTab, distmatTab, featweightsTab};
	for (auto v : views) {
		// connect singnalling into view
		connect(&hub, &CentralHub::newDataset, v, &Viewer::addDataset);
		// see below; disable when it works
		connect(this, &MainWindow::datasetSelected, v, &Viewer::selectDataset);
		connect(this, &MainWindow::partitionsToggled, v, &Viewer::inTogglePartitions);
		connect(&hub.proteins, &ProteinDB::markersToggled, v, &Viewer::inToggleMarkers);

		// connect signalling out of view
		connect(v, &Viewer::markerToggled, this, &MainWindow::toggleMarker);
		connect(v, &Viewer::cursorChanged, profiles, &ProfileWidget::updateProteins);
		connect(v, &Viewer::orderRequested, &hub, &CentralHub::changeOrder);
		connect(v, qOverload<QGraphicsView*, QString>(&Viewer::exportRequested), renderSlot);
		connect(v, qOverload<QGraphicsScene*, QString>(&Viewer::exportRequested), renderSlot);

		// gui synchronization between views
		for (auto v2 : views) {
			if (v2 == v)
				continue;
			connect(v, &Viewer::orderRequested, v2, &Viewer::changeOrder);
		}

		// set initial state
		emit v->inUpdateColorset(hub.colorset());
		emit v->inTogglePartitions(actionShowPartition->isChecked());
	}

	/* experimental: put to sleep when not visible
	connect(tabWidget, &QTabWidget::currentChanged, [this] () {
		auto current = qobject_cast<Viewer*>(tabWidget->currentWidget());
		for (auto v : views) {
			if (v == current)
				v->selectDataset(data ? data->id() : 0);
			else
				v->selectDataset(0);
		}
	});
	connect(this, &MainWindow::datasetSelected, [this] (unsigned id) {
		auto current = qobject_cast<Viewer*>(tabWidget->currentWidget());
		if (current)
			current->selectDataset(id);
	});
	/// does not work right now with heatmap, distmap, feattab. why? */

	setupMarkerControls();
	setupSignals(); // after setupToolbar(), signal handlers rely on initialized actions
	setupActions();

	// initialize widgets to be empty & most-restrictive
	updateState({});
}

void MainWindow::setupToolbar()
{
	// setup datasets selection model+view
	datasetTree = new QTreeWidget(this);
	datasetTree->setHeaderHidden(true);
	datasetTree->setFrameShape(QFrame::Shape::NoFrame);
	datasetTree->setSelectionMode(QTreeWidget::SelectionMode::NoSelection);
	datasetTree->setItemsExpandable(false);
	datasetSelect->setModel(datasetTree->model());
	datasetSelect->setView(datasetTree);

	// put datasets and some space before partition area
	auto anchor = actionShowPartition;
	toolBar->insertWidget(anchor, datasetLabel);
	toolbarActions.datasets = toolBar->insertWidget(anchor, datasetSelect);
	toolBar->insertSeparator(anchor);

	// fill-up partition area
	toolBar->insertWidget(anchor, partitionLabel);
	toolbarActions.partitions = toolBar->insertWidget(anchor, partitionSelect);
	toolbarActions.granularity = toolBar->addWidget(granularitySlider);
	toolbarActions.famsK = toolBar->addWidget(famsKSlider);

	// remove container we picked from
	topBar->deleteLater();
}

void MainWindow::setupSignals()
{
	/* error dialogs */
	connect(&hub, &CentralHub::ioError, this, &MainWindow::displayError);
	connect(io, &FileIO::ioError, this, &MainWindow::displayError);

	/* notifications from Protein db */
	connect(&hub.proteins, &ProteinDB::proteinAdded, this, &MainWindow::addProtein);
	connect(&hub.proteins, &ProteinDB::markersToggled, this, [this] (auto ids, bool present) {
		auto state = present ? Qt::Checked : Qt::Unchecked;
		for (auto id : ids)
			this->markerItems.at(id)->setCheckState(state);
	});

	/* notifications from data/storage thread */
	connect(&hub.store, &Storage::newAnnotations, this, [this] (auto name, bool loaded) {
		if (loaded) { // already pre-selected, need to reflect that
			QSignalBlocker _(partitionSelect);
			partitionSelect->addItem(name);
			partitionSelect->setCurrentText(name);
		} else {
			partitionSelect->addItem(name);
		}
	});
	connect(&hub.store, &Storage::newHierarchy, this, [this] (auto name, bool loaded) {
		auto n = name + hierarchyPostfix;
		partitionSelect->addItem(n);
		if (loaded) { // already pre-selected, need to reflect that
			QSignalBlocker _(partitionSelect);
			partitionSelect->setCurrentText(n);
		}
	});

	connect(&hub, &CentralHub::newDataset, this, &MainWindow::newDataset);

	/* selecting dataset */
	connect(datasetSelect, qOverload<int>(&QComboBox::activated), [this] {
		setDataset(datasetSelect->currentData().value<Dataset::Ptr>());
	});
	connect(this, &MainWindow::datasetSelected, &hub, &CentralHub::setCurrent);
	connect(this, &MainWindow::datasetSelected, [this] { profiles->setData(data); });
	connect(this, &MainWindow::datasetSelected, this, &MainWindow::setSelectedDataset);

	/* selecting/altering partition */
	connect(partitionSelect, qOverload<int>(&QComboBox::activated), [this] {
		// clear partition-type dependant state
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setEnabled(false);

		// special items (TODO: better use an enum here, maybe include hierarchies)
		if (partitionSelect->currentData().isValid()) {

			auto v = partitionSelect->currentData().value<int>();
			if (v == 0) {
				data->cancelFAMS(); // TODO
				emit hub.clearClusters();
			} else if (v == 1) {
				// TODO
				data->changeFAMS((unsigned)famsKSlider->value() * 0.01f);
				emit hub.runFAMS();
				toolbarActions.famsK->setVisible(true);
				actionExportAnnotations->setEnabled(true);
			}
			return;
		}

		// not FAMS? cancel it in case it is running TODO
		data->cancelFAMS();

		// regular items: identified by name
		auto name = partitionSelect->currentText();
		if (name.isEmpty())
			return;

		bool isHierarchy = name.endsWith(hierarchyPostfix);
		if (isHierarchy) {
			auto n = name.chopped(strlen(hierarchyPostfix));
			emit hub.readHierarchy(n);
			emit hub.calculatePartition((unsigned)granularitySlider->value());
			toolbarActions.granularity->setVisible(true);
			actionExportAnnotations->setEnabled(true);
		} else {
			emit hub.readAnnotations(name);
		}
	});
	connect(granularitySlider, &QSlider::valueChanged, &hub, &CentralHub::calculatePartition);
	connect(famsKSlider, &QSlider::valueChanged, [this] (int v) {
		data->changeFAMS(v * 0.01f); // reconfigure from outside (this thread) // TODO
		emit hub.runFAMS(); // start calculation inside data thread
	});
}

void MainWindow::setupActions()
{
	/* Shortcuts (standard keys not available in UI Designer) */
	actionLoadDataset->setShortcut(QKeySequence::StandardKey::Open);
	actionHelp->setShortcut(QKeySequence::StandardKey::HelpContents);
	actionQuit->setShortcut(QKeySequence::StandardKey::Quit);

	/* Buttons to be wired to actions */
	loadMarkersButton->setDefaultAction(actionLoadMarkers);
	saveMarkersButton->setDefaultAction(actionSaveMarkers);
	clearMarkersButton->setDefaultAction(actionClearMarkers);

	connect(actionQuit, &QAction::triggered, [] { QApplication::exit(); });
	connect(actionHelp, &QAction::triggered, this, &MainWindow::showHelp);

	// TODO a little hack to allow loading of abundance values, we would need
	// a proper fancy loading dialog in the future…
	auto loader = [this] (const QString& featureCol) {
		auto filename = io->chooseFile(FileIO::OpenDataset);
		if (filename.isEmpty())
			return;
		hub.importDataset(filename, featureCol);
	};
	connect(actionLoadDataset, &QAction::triggered, [l=loader] { l("Dist"); });
	connect(actionLoadDatasetAbundance, &QAction::triggered, [l=loader] { l("AbundanceLeft"); });

	connect(actionLoadDescriptions, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenDescriptions);
		if (filename.isEmpty())
			return;
		hub.importDescriptions(filename);
	});
	connect(actionLoadAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenClustering);
		if (filename.isEmpty())
			return;
		auto filetype = QFileInfo(filename).suffix();
		if (filetype == "json")
			hub.importHierarchy(filename);
		else
			hub.importAnnotations(filename);
	});
	connect(actionExportAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveAnnotations);
		if (filename.isEmpty())
			return;

		emit hub.exportAnnotations(filename);
	});
	connect(actionShowPartition, &QAction::toggled, this, &MainWindow::partitionsToggled);
	connect(actionClearMarkers, &QAction::triggered, &hub.proteins, &ProteinDB::clearMarkers);
	connect(actionLoadMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenMarkers);
		if (filename.isEmpty())
			return;
		hub.store.importMarkers(filename);
	});
	connect(actionSaveMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveMarkers);
		if (filename.isEmpty())
			return;
		hub.store.exportMarkers(filename);
	});

	connect(actionSplice, &QAction::triggered, [this] {
		if (!data)
			return;
		auto s = new SpawnDialog(data, this);
		// spawn dialog deletes itself, should also kill connection+lambda, right?
		connect(s, &SpawnDialog::spawn, [this] (auto data, auto& config) {
			emit hub.spawn(data, config, dimredTab->currentMethod());
		});
	});
}

void MainWindow::setupMarkerControls()
{
	/* setup completer with empty model */
	auto m = new QStandardItemModel(this);
	auto cpl = new QCompleter(m, this);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	// we expect model entries to be sorted
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	cpl->setCompletionMode(QCompleter::InlineCompletion);
	protSearch->setCompleter(cpl);
	protList->setModel(cpl->completionModel());

	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		auto id = ProteinId(i->data().toInt());
		if (i->checkState() == Qt::Checked)
			hub.proteins.addMarker(id);
		else
			hub.proteins.removeMarker(id);
	});

	auto toggler = [m] (QModelIndex i) {
		if (!i.isValid())
			return; // didn't click on a row, e.g. clicked on a checkmark
		auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
		if (!proxy)
			return; // sorry, can't do this!
		auto item = m->itemFromIndex(proxy->mapToSource(i));
		if (!item->isEnabled())
			return;
		item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
	};

	/* Allow to toggle check state by click */
	connect(protList, &QListView::clicked, toggler);

	/* Allow to toggle by pressing <Enter> in protSearch */
	connect(protSearch, &QLineEdit::returnPressed, [this, cpl, toggler] {
		if (cpl->currentCompletion() == protSearch->text()) // still relevant
			toggler(cpl->currentIndex());
	});

	/* Implement behavior such as updating the filter also when a character is removed.
	 * It seems by default, QCompleter only updates when new characters are added. */
	QString lastText;
	connect(protSearch, &QLineEdit::textEdited, [cpl, lastText] (const QString &text) mutable {
		if (text.length() < lastText.length()) {
			cpl->setCompletionPrefix(text);
		}
		lastText = text;
	});
}

void MainWindow::updateState(Dataset::Touched affected)
{
	resetMarkerControls();

	if (!data) {
		/* hide and disable widgets that need data or even more */
		actionSplice->setEnabled(false);
		toolbarActions.partitions->setEnabled(false);
		actionShowPartition->setChecked(false);
		actionShowPartition->setEnabled(false);
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setEnabled(false);
		return;
	}

	/* re-enable actions that depend only on data */
	actionSplice->setEnabled(true);
	toolbarActions.partitions->setEnabled(true);

	/* structure */
	auto d = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>();
	if (affected & Dataset::Touch::CLUSTERS) {
		bool haveClustering = !s->clustering.empty();
		actionShowPartition->setEnabled(haveClustering);
		actionShowPartition->setChecked(haveClustering);
	}
	if (affected & Dataset::Touch::HIERARCHY) {
		if (!s->hierarchy.empty()) {
			auto reasonable = std::min(d->protIds.size(), s->hierarchy.size()) / 4;
			granularitySlider->setMaximum(reasonable);
		}
	}

	/* reset partitions: none except inbuilt mean-shift */
	// TODO we need to keep track what is available per-dataset in storage
	partitionSelect->clear();
	partitionSelect->addItem("None", {0});
	partitionSelect->addItem("Adaptive Mean Shift", {1});

}

void MainWindow::newDataset(Dataset::Ptr dataset)
{
	/* add to datasets */
	auto conf = dataset->config();
	auto parent = (conf.parent ? datasetItems.at(conf.parent)
	                           : datasetTree->invisibleRootItem()); // top level
	auto item = new QTreeWidgetItem(parent);
	item->setExpanded(true);
	item->setText(0, conf.name);
	item->setData(0, Qt::UserRole, QVariant::fromValue(dataset));
	datasetItems[conf.id] = item;

	/* auto select */
	setDataset(dataset);
	toolbarActions.datasets->setEnabled(true);
}

void MainWindow::setDataset(Dataset::Ptr selected)
{
	if (data == selected)
		return;

	// disconnect from old data
	if (data)
		disconnect(data.get());

	// swap
	data = selected;
	if (data)
		// tell hub & views before our GUI might send more signals
		emit datasetSelected(data ? data->id() : 0);

	// update own GUI state once
	updateState(Dataset::Touch::ALL);

	// wire further updates
	if (data)
		connect(data.get(), &Dataset::update, this, &MainWindow::updateState);

	// TODO wronge place to do this in new storage concept
	setFilename(data ? hub.store.name() : "");
}

void MainWindow::resetMarkerControls()
{
	/* enable only proteins that are found in current dataset */
	if (data) {
		auto d = data->peek<Dataset::Base>();
		for (auto& [id, item] : markerItems)
			item->setEnabled(d->protIndex.count(id));
	} else {
		for (auto& [id, item] : markerItems)
			item->setEnabled(false);
	}
}

void MainWindow::finalizeMarkerItems()
{
	if (markerWidget->isEnabled()) // already in good state
		return;

	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->sort(0);
	markerWidget->setEnabled(true); // we are in good state now
}

void MainWindow::setFilename(QString name)
{
	if (name.isEmpty()) {
		setWindowTitle("Belki");
		setWindowFilePath({});
		return;
	}

	setWindowTitle(QString("%1 – Belki").arg(name));
	setWindowFilePath(name);
}

void MainWindow::setSelectedDataset(unsigned index)
{
	/* this is a tad tricky to do due to Qt interface limitations */
	// make item current in tree to get hold of its index
	datasetTree->setCurrentItem(datasetItems.at(index));
	// make item's parent reference point and provide index in relation to parent
	datasetSelect->setRootModelIndex(datasetTree->currentIndex().parent());
	datasetSelect->setCurrentIndex(datasetTree->currentIndex().row());
	// reset combobox to display full tree again
	datasetTree->setCurrentItem(datasetTree->invisibleRootItem());
	datasetSelect->setRootModelIndex(datasetTree->currentIndex());
}

void MainWindow::showHelp()
{
	QMessageBox box(this);
	box.setWindowTitle("Help");
	box.setIcon(QMessageBox::Information);
	QFile helpText(":/help.html");
	helpText.open(QIODevice::ReadOnly);
	box.setText(helpText.readAll());
	box.setWindowModality(Qt::WindowModality::WindowModal); // sheet in OS X
	box.exec();
}

void MainWindow::displayError(const QString &message)
{
	QMessageBox::critical(this, "An error occured", message);
}

void MainWindow::addProtein(ProteinId id)
{
	/* setup new item */
	auto item = new QStandardItem;
	item->setText(hub.proteins.peek()->proteins[id].name);
	item->setData(id);
	item->setCheckable(true);
	item->setCheckState(Qt::Unchecked);
	// expect new protein not to be in current dataset (yet)
	item->setEnabled(false);

	/* add item to model */
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->appendRow(item);
	markerItems[id] = item;

	/* ensure items are sorted in the end, but defer sorting */
	markerWidget->setEnabled(false); // we are "dirty"
	QTimer::singleShot(0, this, &MainWindow::finalizeMarkerItems);
}

void MainWindow::toggleMarker(ProteinId id, bool present)
{
	markerItems.at(id)->setCheckState(present ? Qt::Checked : Qt::Unchecked);
}
