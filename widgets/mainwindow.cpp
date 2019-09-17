#include "mainwindow.h"
#include "centralhub.h"
#include "storage.h"
#include "fileio.h"
#include "profiles/profilewindow.h"
#include "widgets/spawndialog.h"

#include "scatterplot/dimredtab.h"
#include "scatterplot/scattertab.h"
#include "heatmap/heatmaptab.h"
#include "distmat/distmattab.h"
#include "featweights/featweightstab.h"

#include <QTreeWidget>
#include <QFileInfo>
#include <QDir>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QLabel>
#include <QToolButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QMimeData>
#include <QWidgetAction>

MainWindow::MainWindow(CentralHub &hub) :
    hub(hub),
    io(new FileIO(this)) // cleanup by QObject
{
	setupUi(this);
	setupToolbar();
	setupTabs();
	setupMarkerControls();
	setupSignals(); // after setupToolbar(), signal handlers rely on initialized actions
	setupActions();

	// initialize widgets to be empty & most-restrictive
	updateState(Dataset::Touch::BASE);
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
	auto anchor = actionShowStructure;
	toolBar->insertWidget(anchor, datasetLabel);
	toolbarActions.datasets = toolBar->insertWidget(anchor, datasetSelect);
	toolBar->insertSeparator(anchor);

	// fill-up partition area
	structureSelect->addItem("None", 0);
	structureSelect->addItem(QIcon(":/icons/type-meanshift.svg"), "Adaptive Mean Shift", -1);
	toolBar->insertWidget(anchor, structureLabel);
	toolbarActions.structure = toolBar->insertWidget(anchor, structureSelect);
	toolbarActions.granularity = toolBar->addWidget(granularitySlider);
	toolbarActions.famsK = toolBar->addWidget(famsKSlider);

	// remove container we picked from
	topBar->deleteLater();
}

void MainWindow::setupTabs()
{
	// "add tab" menu
	auto *menu = new QMenu("Add tab");
	for (const auto &[type, name] : tabTitles)
		menu->addAction(name, [this, t=type] { addTab(t); });

	// integrate into main menu (does not take ownership)
	menuView->insertMenu(actionCloseAllTabs, menu);

	// button for adding tabs
	auto *btn = new QToolButton;
	btn->setIcon(QIcon::fromTheme("tab-new"));
	btn->setMenu(menu); // does not take ownership
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
	btn->setMinimumSize(btn->sizeHint()); // ensure button keeps showing when zero tabs
	tabWidget->setCornerWidget(btn);

	// setup tab closing
	connect(tabWidget, &QTabWidget::tabCloseRequested,
	        [this] (auto index) { delete tabWidget->widget(index); });

	// initial tabs
	addTab(Tab::DIMRED);
	addTab(Tab::SCATTER);
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
	connect(&hub.proteins, &ProteinDB::structureAvailable, this,
	        [this] (unsigned id, QString name, bool select) {
		auto icon = (hub.proteins.peek()->isHierarchy(id) ? "hierarchy" : "annotations");
		structureSelect->addItem(QIcon(QString(":/icons/type-%1.svg").arg(icon)), name, id);
		if (select)
			selectStructure((int)id);
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
	connect(structureSelect, qOverload<int>(&QComboBox::activated), [this] {
		selectStructure(structureSelect->currentData().value<int>());
	});
	connect(granularitySlider, &QSlider::valueChanged, &hub, &CentralHub::createPartition);
	connect(famsKSlider, &QSlider::valueChanged, [this] (int v) {
		hub.runFAMS(v * 0.01f);
	});

	/* changing order settings */
	connect(this, &MainWindow::orderChanged, &hub, &CentralHub::changeOrder);
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
	connect(actionCloseAllTabs, &QAction::triggered, [this] {
		for (int i = tabWidget->count() - 1; i >= 0; --i)
			delete tabWidget->widget(i);
		tabHistory.clear();
	});

	connect(actionLoadDataset, &QAction::triggered, [this] { openFile(Input::DATASET); });
	connect(actionLoadDatasetAbundance, &QAction::triggered, [this] { openFile(Input::DATASET_RAW); });
	connect(actionLoadDescriptions, &QAction::triggered, [this] { openFile(Input::DESCRIPTIONS); });
	connect(actionLoadMarkers, &QAction::triggered, [this] { openFile(Input::MARKERS); });
	connect(actionImportStructure, &QAction::triggered, [this] { openFile(Input::STRUCTURE); });

	connect(actionSaveMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveMarkers);
		if (filename.isEmpty())
			return;
		hub.store.exportMarkers(filename);
	});
	connect(actionExportAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveAnnotations);
		if (filename.isEmpty())
			return;

		hub.exportAnnotations(filename);
	});
	connect(actionPersistAnnotations, &QAction::triggered, [this] {
		if (!data)
			return;

		/* we do it straight away, we keep our own copy while letting the user
		   edit the name, so nothing can happen to it in the meantime */
		auto clustering = std::make_unique<Annotations>
		        (data->peek<Dataset::Structure>()->clustering);
	    auto name = QInputDialog::getText(this, "Keep snapshot of current clustering",
		                                  "Please provide a name:", QLineEdit::Normal,
		                                  clustering->name);
	    if (name.isEmpty())
			return; // user cancelled

		clustering->name = name;
		hub.proteins.addAnnotations(std::move(clustering), false, true);
	});
	connect(actionClearMarkers, &QAction::triggered, &hub.proteins, &ProteinDB::clearMarkers);

	connect(actionSplice, &QAction::triggered, [this] {
		if (!data)
			return;
		auto s = new SpawnDialog(data, this);
		// spawn dialog deletes itself, should also kill connection+lambda, right?
		connect(s, &SpawnDialog::spawn, [this] (auto data, auto& config) {
			emit hub.spawn(data, config); // TODO change mechanic, dimredTab->currentMethod());
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

void MainWindow::toggleMarker(ProteinId id, bool present)
{
	markerItems.at(id)->setCheckState(present ? Qt::Checked : Qt::Unchecked);
}

void MainWindow::addTab(MainWindow::Tab type)
{
	Viewer *v;
	switch (type) {
	case Tab::DIMRED: v = new DimredTab; break;
	case Tab::SCATTER: v = new ScatterTab; break;
	case Tab::HEATMAP: v = new HeatmapTab; break;
	case Tab::DISTMAT: v = new DistmatTab; break;
	case Tab::FEATWEIGHTS: v = new FeatweightsTab; break;
	}

	// connect singnalling into view
	connect(&hub, &CentralHub::newDataset, v, &Viewer::addDataset);
	/* use queued conn. to ensure the views get the newDataset signal _first_! */
	connect(this, &MainWindow::datasetSelected, v, &Viewer::selectDataset, Qt::QueuedConnection);
	connect(actionShowStructure, &QAction::toggled, v, &Viewer::inTogglePartitions);
	connect(actionUseOpenGL, &QAction::toggled, v, &Viewer::inToggleOpenGL);
	connect(&hub.proteins, &ProteinDB::markersToggled, v, &Viewer::inToggleMarkers);
	connect(this, &MainWindow::orderChanged, v, &Viewer::changeOrder);

	// connect signalling out of view
	connect(v, &Viewer::markerToggled, this, &MainWindow::toggleMarker);
	connect(v, &Viewer::cursorChanged, profiles, &ProfileWidget::updateProteins);

	auto renderSlot = [this] (auto r, auto d) { io->renderToFile(r, {title, d}); };
	connect(v, qOverload<QGraphicsView*, QString>(&Viewer::exportRequested), renderSlot);
	connect(v, qOverload<QGraphicsScene*, QString>(&Viewer::exportRequested), renderSlot);

	connect(v, &Viewer::orderRequested, this, &MainWindow::orderChanged);

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

	// set initial state
	emit v->inUpdateColorset(hub.colorset());
	emit v->inTogglePartitions(actionShowStructure->isChecked());
	emit v->inToggleOpenGL(actionUseOpenGL->isChecked());
	for (auto &[_, d] : hub.datasets())
		v->addDataset(d);
	if (data)
		v->selectDataset(data->id());

	auto title = tabTitles.at(type);
	auto count = tabHistory.count(type);
	if (count)
		title.append(QString(" (%1)").arg(count + 1));
	tabHistory.insert(type);

    tabWidget->addTab(v, title);
	tabWidget->setCurrentWidget(v);
}

void MainWindow::updateState(Dataset::Touched affected)
{
	if (affected & Dataset::Touch::BASE)
		resetMarkerControls();

	if (!data) {
		/* hide and disable widgets that need data or even more */
		actionSplice->setEnabled(false);
		actionShowStructure->setChecked(false);
		actionShowStructure->setEnabled(false);
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setEnabled(false);
		actionPersistAnnotations->setEnabled(false);
		return;
	}

	/* re-enable actions that depend only on data */
	actionSplice->setEnabled(true);

	/* structure */
	auto d = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>();
	if (affected & Dataset::Touch::CLUSTERS) {
		bool haveClustering = !s->clustering.empty();
		actionShowStructure->setEnabled(haveClustering);
		actionShowStructure->setChecked(haveClustering);
		actionExportAnnotations->setEnabled(haveClustering);
		bool computedClustering = haveClustering && structureSelect->currentData() < 1;
		actionPersistAnnotations->setEnabled(computedClustering);
	}
	if (affected & Dataset::Touch::HIERARCHY) {
		if (!s->hierarchy.clusters.empty()) {
			auto reasonable = std::min(d->protIds.size(), s->hierarchy.clusters.size()) / 4;
			granularitySlider->setMaximum(reasonable);
		}
	}
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

void MainWindow::selectStructure(int id)
{
	structureSelect->setCurrentIndex(structureSelect->findData(id));

	// clear type-dependant state
	toolbarActions.granularity->setVisible(false);
	toolbarActions.famsK->setVisible(false);

	if (id == 0) { // "None"
		hub.applyAnnotations(0);
		return;
	} else if (id == -1) { // Mean shift
		hub.runFAMS(famsKSlider->value() * 0.01f);
		toolbarActions.famsK->setVisible(true);
		return;
	}

	/* regular items */

	// check between hierarchy and annotations
	if (this->hub.proteins.peek()->isHierarchy((unsigned)id)) {
		hub.applyHierarchy((unsigned)id, (unsigned)granularitySlider->value());
		toolbarActions.granularity->setVisible(true);
	} else {
		hub.applyAnnotations((unsigned)id);
	}
}

void MainWindow::openFile(Input type, QString fn)
{
	/* no preset filename – ask user to select */
	if (fn.isEmpty()) {
		const std::map<Input, FileIO::Role> mapping = {
		    {Input::DATASET, FileIO::OpenDataset},
		    {Input::DATASET_RAW, FileIO::OpenDataset},
		    {Input::MARKERS, FileIO::OpenMarkers},
		    {Input::DESCRIPTIONS, FileIO::OpenDescriptions},
		    {Input::STRUCTURE, FileIO::OpenStructure},
		};
		fn = io->chooseFile(mapping.at(type));
		if (fn.isEmpty())
			return; // nothing selected
	}

	switch (type) {
	case Input::DATASET:      hub.importDataset(fn, "Dist"); break;
	case Input::DATASET_RAW:  hub.importDataset(fn, "AbundanceLeft"); break;
	case Input::MARKERS:      hub.store.importMarkers(fn); break;
	case Input::DESCRIPTIONS: hub.importDescriptions(fn); break;
	case Input::STRUCTURE: {
		if (QFileInfo(fn).suffix() == "json")
			hub.importHierarchy(fn);
		else
			hub.importAnnotations(fn);
		break;
	}
	}
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

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	auto urls = event->mimeData()->urls();
	for (auto i : qAsConst(urls)) {
		if (!urls.front().toLocalFile().isEmpty()) {
			event->acceptProposedAction(); // we are given at least one filename
			break;
		}
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	auto urls = event->mimeData()->urls();
	auto title = (urls.size() == 1 ? "Open file as…"
	                               : QString("Open %1 files as…").arg(urls.size()));

	QMenu chooser(title, this);
	auto label = new QLabel(QString("<b>%1</b>").arg(title));
	label->setAlignment(Qt::AlignCenter);
	label->setMargin(2);
	auto t = new QWidgetAction(&chooser);
	t->setDefaultWidget(label);
	chooser.addAction(t);

	std::map<QAction*, Input> actions = {
	{chooser.addAction("Dataset"), Input::DATASET},
	{chooser.addAction("Abundance Dataset"), Input::DATASET_RAW},
	{chooser.addAction("Structure"), Input::STRUCTURE},
	{chooser.addAction("Marker List"), Input::MARKERS},
	{chooser.addAction("Descriptions"), Input::DESCRIPTIONS},
	};
	chooser.addSeparator();
	chooser.addAction(style()->standardIcon(QStyle::SP_DialogCancelButton), "Cancel");

	auto choice = chooser.exec(mapToGlobal(event->pos()), t);
	auto action = actions.find(choice);
	if (action == actions.end())
		return; // do not accept event

	for (auto i : qAsConst(urls)) {
		auto filename = i.toLocalFile();
		if (!filename.isEmpty())
			openFile(action->second, filename);
	}

	event->setDropAction(Qt::DropAction::CopyAction);
	event->accept();
}
