#include "mainwindow.h"
#include "spawndialog.h"
#include "jobstatus.h"
#include "famscontrol.h"
#include "../profiles/profilewindow.h"

#include "windowstate.h"
#include "datahub.h"
#include "fileio.h"
#include "jobregistry.h"
#include "../storage/storage.h"

#include "../scatterplot/dimredtab.h"
#include "../scatterplot/scattertab.h"
#include "../heatmap/heatmaptab.h"
#include "../distmat/distmattab.h"
#include "../profiles/profiletab.h"
#include "../profiles/bnmstab.h"
#include "../featweights/featweightstab.h"

#include <QTreeView>
#include <QFileInfo>
#include <QDir>
#include <QCompleter>
#include <QAbstractProxyModel>
#include <QStandardItemModel>
#include <QLabel>
#include <QToolButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QMimeData>
#include <QWidgetAction>
#include <QShortcut>
#include <QDateTime>
#include <QClipboard>

MainWindow::MainWindow(GuiState &owner)
    : state(std::make_shared<WindowState>(owner))
{
	setupUi(this);
	profiles->init(state);
	setupModelViews(); // before setupTabs(), tabs may need models
	setupToolbar();
	setupTabs();
	setupSignals(); // after setupToolbar(), signal handlers rely on initialized actions
	setupActions();

	// initialize window state
	actionShowStructure->setChecked(state->showAnnotations);
	actionUseOpenGL->setChecked(state->useOpenGl);
	auto p = state->hub().projectMeta();
	setName(p.name, p.path);

	// initialize widgets to no-dataset-selected state
	updateState(Dataset::Touch::BASE);
}

void MainWindow::setupModelViews()
{
	/** Proteins/Markers **/

	/* setup completer with model */
	auto m = &markerModel;
	auto cpl = new QCompleter(m, this);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	// we expect model entries to be sorted
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	cpl->setCompletionMode(QCompleter::InlineCompletion);
	protSearch->setCompleter(cpl);
	protList->setModel(cpl->completionModel());

	/* Allow to toggle check state by click */
	connect(protList, &QListView::clicked, this, &MainWindow::markerFlipped);

	/* Allow to toggle by pressing <Enter> in protSearch */
	connect(protSearch, &QLineEdit::returnPressed, [this, cpl] {
		if (cpl->currentCompletion() == protSearch->text()) // still relevant
			emit markerFlipped(cpl->currentIndex());
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

	/* setup context menu on protein list */
	protList->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
	connect(protList, &QListView::customContextMenuRequested, this, [this] (const QPoint &pos) {
		QModelIndex index = protList->indexAt(pos);
		if (index.isValid()) {
			auto id = protList->model()->data(index, Qt::UserRole + 1).toUInt();
			state->proteinMenu(id)->exec(QCursor::pos());
		}
	});

	/* change action state according to what's going on */
	auto recognizeEmpty = [this,m] {
		bool isEmpty = m->rowCount() == 0;
		actionCopyProtlistToClipboard->setDisabled(isEmpty);
	};
	auto recognizeNoMarkers = [this] {
		bool haveMarkers = !state->proteins().peek()->markers.empty();
		for (auto i : {actionSaveMarkers, actionClearMarkers})
			i->setEnabled(haveMarkers);
	};
	connect(m, &QAbstractItemModel::rowsRemoved, recognizeEmpty);
	connect(m, &QAbstractItemModel::rowsInserted, recognizeEmpty);
	connect(&state->proteins(), &ProteinDB::markersToggled, recognizeNoMarkers);

	/** Datasets **/
	/* setup datasets selection view */
	datasetTree = new QTreeView(this);
	datasetTree->setHeaderHidden(true);
	datasetTree->setFrameShape(QFrame::Shape::NoFrame);
	datasetTree->setSelectionMode(QTreeView::SelectionMode::NoSelection);
	datasetTree->setItemsExpandable(false);
	datasetTree->setRootIsDecorated(false); // avoid impression of common root to top-level datasets

	/* setup context menu on datasets */
	datasetTree->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
	connect(datasetTree, &QTreeView::customContextMenuRequested, this, [this] (const QPoint &pos) {
		QModelIndex index = datasetTree->indexAt(pos);
		if (index.isValid()) {
			auto m = datasetSelect->model();
			auto name = m->data(index).toString();
			QMenu popup(QString{"Dataset %1"}.arg(name), this);
			auto rename = popup.addAction(QIcon::fromTheme("edit-rename"), "Re&name");
			auto remove = popup.addAction(QIcon::fromTheme("edit-delete"), "&Remove");
			if (m->hasChildren(index))
				remove->setText("&Remove with descendants");
			auto selected = popup.exec(datasetTree->viewport()->mapToGlobal(pos));
			if (selected == rename) {
				/* datasetTree->edit() does not work as the view is wired to the combobox */
				QString newName = QInputDialog::getText(
				                      this, QString("Rename Dataset %1").arg(name),
				                      "Enter dataset name:", QLineEdit::Normal, name);
				if (newName != name && !newName.isEmpty()) {
					m->setData(index, newName);
					/* this could also be wired through model::dataChanged signal,
					 * however it would be a bit complicated. Future work. */
					m->data(index, Qt::UserRole + 1).value<Dataset::Ptr>()->setName(newName);
				}
			}
			if (selected == remove) {
				state->hub().removeDataset(m->data(index, Qt::UserRole).toUInt());
				if (datasetTree->model()->rowCount() == 0)
					datasetSelect->hidePopup();
			}
		}
	});

	datasetSelect->setView(datasetTree);
}

void MainWindow::setupToolbar()
{
	/* put datasets into main toolbar */
	mainToolbar->addWidget(datasetLabel);
	toolbarActions.datasets = mainToolbar->addWidget(datasetSelect);

	/* fill-up structure area */
	famsControl = new FAMSControl(this);
	famsControl->setWindowState(state);
	connect(this, &MainWindow::datasetSelected, famsControl, &Viewer::selectDataset,
	        Qt::QueuedConnection);
	connect(this, &MainWindow::datasetDeselected, famsControl, &Viewer::deselectDataset);

	auto anchor = actionShowStructure;
	structToolbar->insertWidget(anchor, structureLabel);
	toolbarActions.structure = structToolbar->insertWidget(anchor, structureSelect);

	// hierarchy part, bit complicated
	auto &hgrp = toolbarActions.hierarchy;
	hgrp = new QActionGroup(structToolbar);
	hgrp->setVisible(false); // not shown by default
	hgrp->setExclusive(false);
	auto granularityAction = new QWidgetAction(structToolbar);
	granularityAction->setDefaultWidget(granularitySlider);
	for (auto i : {(QAction*)granularityAction, actionPruneClusters})
		hgrp->addAction(i);
	structToolbar->addActions(hgrp->actions());

	// FAMS part
	toolbarActions.fams = structToolbar->addWidget(famsControl->getWidget());
	toolbarActions.fams->setVisible(false);

	/* add background job indicator in job toolbar, right-aligned with spacer trick */
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	jobToolbar->addWidget(spacer);
	jobWidget = new JobStatus();
	jobToolbar->addWidget(jobWidget);
	state->jobMonitors.push_back(jobWidget);

	// remove container we picked from
	stockpile->deleteLater();
}

void MainWindow::setupTabs()
{
	// "add tab" menu
	auto menu = new QMenu("Add tab", this);
	for (const auto &[type, name] : tabTitles)
		menu->addAction(name, [this, t=type] { addTab(t); });

	// integrate into main menu (does not take ownership)
	actionAddTab->setMenu(menu);

	// button for adding tabs
	auto *btn = new QToolButton;
	btn->setDefaultAction(actionAddTab);
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
	btn->setShortcut(QKeySequence::StandardKey::AddTab);
	tabWidget->setCornerWidget(btn);

	// setup tab closing
	connect(tabWidget, &QTabWidget::tabCloseRequested,
	        [this] (auto index) { delete tabWidget->widget(index); });

	// setup tab switching shortcuts (Alt+<Number>)
	for (char i = 0, k ='1'; i < 9; ++i, ++k) {
		auto shortcut = new QShortcut(QKeySequence(QString("Alt+") + k), this);
		connect(shortcut, &QShortcut::activated, [this, i] {
			if (tabWidget->count() > i)
				tabWidget->setCurrentIndex(i);
		});
	}

	// initial tabs
	addTab(Tab::HEATMAP);
	addTab(Tab::DISTMAT);
	addTab(Tab::DIMRED);
	addTab(Tab::PROFILES);
	tabWidget->setCurrentIndex(0);
}

void MainWindow::setupSignals()
{
	/* selecting dataset */
	connect(datasetSelect, qOverload<int>(&QComboBox::activated), [this] {
		setDataset(datasetSelect->currentData(Qt::UserRole + 1).value<Dataset::Ptr>());
	});
	connect(this, &MainWindow::datasetSelected, [this] { profiles->setData(data); });
	connect(this, &MainWindow::datasetSelected, this, &MainWindow::setSelectedDataset);

	/* changing window settings */
	connect(actionShowStructure, &QAction::toggled, [this] (bool on) {
		state->showAnnotations = on;
		emit state->annotationsToggled();
	});
	connect(actionUseOpenGL, &QAction::toggled, [this] (bool on) {
		state->useOpenGl = on;
		emit state->openGlToggled();
	});

	/* selecting/altering partition */
	connect(structureSelect, qOverload<int>(&QComboBox::activated), [this] {
		selectStructure(structureSelect->currentData().value<int>());
	});
	connect(granularitySlider, &QSlider::valueChanged, [this] (int v) {
		granularitySlider->setToolTip(QString("Granularity: %1").arg(v));
		switchHierarchyPartition((unsigned)v, actionPruneClusters->isChecked());
	});
	connect(actionPruneClusters, &QAction::toggled, [this] (bool on) {
		switchHierarchyPartition((unsigned)granularitySlider->value(), on);
	});
}

void MainWindow::setupActions()
{
	/* Shortcuts (standard keys not available in UI Designer) */
	actionNewProject->setShortcut(QKeySequence::StandardKey::New);
	actionOpenProject->setShortcut(QKeySequence::StandardKey::Open);
	actionSave->setShortcut(QKeySequence::StandardKey::Save);
	actionSaveAs->setShortcut(QKeySequence::StandardKey::SaveAs);
	actionCloseProject->setShortcut(QKeySequence::StandardKey::Close);
	actionHelp->setShortcut(QKeySequence::StandardKey::HelpContents);
	actionQuit->setShortcut(QKeySequence::StandardKey::Quit);

	/* Buttons to be wired to actions */
	copyProtsButton->setDefaultAction(actionCopyProtlistToClipboard);
	onlyMarkersButton->setDefaultAction(actionOnlyMarkers);
	loadMarkersButton->setDefaultAction(actionLoadMarkers);
	saveMarkersButton->setDefaultAction(actionSaveMarkers);
	clearMarkersButton->setDefaultAction(actionClearMarkers);

	connect(actionOnlyMarkers, &QAction::toggled, [this] (bool checked) {
		markerModel.onlyMarkers = checked;
		markerModel.invalidateFilter();
	});
	connect(actionCopyProtlistToClipboard, &QAction::triggered, [this] {
		// see also ProfileWidget::actionCopyToClipboard
		auto p = state->proteins().peek();
		auto m = protList->model();
		// build two columns, first for name, second for marker state (x)
		QStringList list;
		for (int i = 0; i < m->rowCount(); ++i) { // we know we have a flat list
			auto id = m->data(m->index(i, 0), Qt::UserRole + 1).toUInt();
			list.append(QString("%1\t%2")
			            .arg(p->proteins.at(id).name).arg(p->isMarker(id) ? "x" : ""));
		}
		QGuiApplication::clipboard()->setText(list.join("\r\n"));
	});

	connect(actionNewProject, &QAction::triggered, this, &MainWindow::newProjectRequested);
	// we do not connect actionSave, which is connected by setName()
	connect(actionSaveAs, &QAction::triggered, [this] { saveProject(true); });
	connect(actionCloseProject, &QAction::triggered, this, &MainWindow::closeProjectRequested);
	connect(actionQuit, &QAction::triggered, this, &MainWindow::quitApplicationRequested);
	connect(actionHelp, &QAction::triggered, this, &MainWindow::showHelp);
	connect(actionAbout, &QAction::triggered, [this] {
		auto date = QDateTime::fromString(PROJECT_DATE, "yyyyMMdd").toString("MMMM d, yyyy");
		auto message = QString("<b>Belki " PROJECT_VERSION "</b><br><br>Built on %1.").arg(date);
		QMessageBox::about(this, "About Belki", message);
	});
	connect(actionNewWindow, &QAction::triggered, this, &MainWindow::newWindowRequested);
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
	connect(actionOpenProject, &QAction::triggered, [this] { openFile(Input::PROJECT); });

	connect(actionSaveMarkers, &QAction::triggered, [this] {
		auto filename = state->io().chooseFile(FileIO::SaveMarkers, this);
		if (filename.isEmpty())
			return;

		Task task{[s=state->hub().store(),filename] { s->exportMarkers(filename); },
			      Task::Type::EXPORT_MARKERS, {filename}};
		JobRegistry::run(task, state->jobMonitors);
	});
	connect(actionExportAnnotations, &QAction::triggered, [this] {
		/* keep own copy while user chooses filename */
		auto localCopy = currentAnnotations();
		if (!localCopy)
			return message({"Cannot export.",
			                "Annotations are still under computation.", GuiMessage::WARNING});
		auto filename = state->io().chooseFile(FileIO::SaveAnnotations, this);
		if (filename.isEmpty())
			return;

		auto name = localCopy->meta.name; // take out before we move!
		Task task{[s=state->hub().store(),filename,a=std::move(*localCopy)]
			       { s->exportAnnotations(filename, a); },
			      Task::Type::EXPORT_ANNOTATIONS, {filename, name}};
		JobRegistry::run(task, state->jobMonitors);
	});
	connect(actionPersistAnnotations, &QAction::triggered, [this] {
		/* keep own copy while user edits the name */
		auto localCopy = currentAnnotations();
		if (!localCopy)
			return message({"Cannot create snapshot.",
			                "Annotations are still under computation.", GuiMessage::WARNING});
	    auto name = QInputDialog::getText(this, "Keep snapshot of current clustering",
		                                  "Please provide a name:", QLineEdit::Normal,
		                                  localCopy->meta.name);
	    if (name.isEmpty())
			return; // user cancelled

		localCopy->meta.name = name;
		Task task{[p=&state->proteins(),a=std::move(*localCopy)]
			       { p->addAnnotations(std::make_unique<Annotations>(std::move(a)), false, true); },
			      Task::Type::PERSIST_ANNOTATIONS, {name}};
		JobRegistry::run(task, state->jobMonitors);
	});
	connect(actionClearMarkers, &QAction::triggered, &state->proteins(), &ProteinDB::clearMarkers);

	connect(actionSplice, &QAction::triggered, [this] {
		if (!data)
			return;
		auto s = new SpawnDialog(data, state, this);
		// spawn dialog deletes itself, should also kill connection+lambda, right?
		connect(s, &SpawnDialog::spawn, [this] (auto source, const auto& config) {
			Task task{[h=&state->hub(),source,config] { h->spawn(source, config); },
				      Task::Type::SPAWN, {config.name}};
			JobRegistry::run(task, state->jobMonitors);
		});
	});
	connect(actionComputeHierarchy, &QAction::triggered, [this] {
		if (!data)
			return;
		Task task{[d=data] { d->computeHierarchy(); },
			      Task::Type::COMPUTE_HIERARCHY, {data->config().name}};
		JobRegistry::run(task, state->jobMonitors);
	});
}
void MainWindow::setDatasetControlModel(QStandardItemModel *m)
{
	datasetTree->model()->disconnect(this);

	datasetTree->setModel(m);
	datasetTree->expandAll(); // model can already have data
	datasetSelect->setModel(datasetTree->model());

	connect(m, &QStandardItemModel::rowsInserted, this, [this] {
		datasetTree->expandAll(); // ensure derived datasets are always visible
	});
}

void MainWindow::setMarkerControlModel(QStandardItemModel *source)
{
	markerModel.setSourceModel(source);
}

void MainWindow::setStructureControlModel(QStandardItemModel *m)
{
	structureSelect->setModel(m);
}

void MainWindow::addTab(MainWindow::Tab type)
{
	/* initialize Viewer */
	Viewer *v(nullptr);
	switch (type) {
	case Tab::DIMRED: v = new DimredTab; break;
	case Tab::SCATTER: v = new ScatterTab; break;
	case Tab::HEATMAP: v = new HeatmapTab; break;
	case Tab::DISTMAT: v = new DistmatTab; break;
	case Tab::PROFILES: v = new ProfileTab; break;
	case Tab::FEATWEIGHTS: v = new FeatweightsTab; break;
	case Tab::BNMS: v = new BnmsTab; break;
	default: return;
	}
	v->setWindowState(state);
	v->setProteinModel(&markerModel);

	/* connect signals */
	// use queued conn. to ensure the views get the *newDataset* signal from hub first!
	connect(this, &MainWindow::datasetSelected, v, &Viewer::selectDataset, Qt::QueuedConnection);
	connect(this, &MainWindow::datasetDeselected, v, &Viewer::deselectDataset);
	connect(v, &Viewer::proteinsHighlighted, profiles,
	        qOverload<std::vector<ProteinId>, const QString&>(&ProfileWidget::updateDisplay));
	auto renderSlot = [this] (auto source, auto desc, bool toFile) {
		if (toFile) {
			auto title = (data ? data->config().name : windowTitle());
			state->io().renderToFile(source, {title, desc});
		} else {
			state->io().renderToClipboard(source);
		}
	};
	connect(v, qOverload<QGraphicsView*,  QString, bool>(&Viewer::exportRequested), renderSlot);
	connect(v, qOverload<QGraphicsScene*, QString, bool>(&Viewer::exportRequested), renderSlot);

	/* propagate data selection */
	if (data)
		v->selectDataset(data->id());

	/* add under unique title */
	auto title = tabTitles.at(type);
	auto count = tabHistory.count(type);
	if (count)
		title.append(QString(" (%1)").arg(count + 1));
	tabHistory.insert(type);

	// note: takes ownership of widget, and with it, viewer v through its internal mechanics
	tabWidget->addTab(v->getWidget(), title);
	tabWidget->setCurrentWidget(v->getWidget());
}

void MainWindow::updateState(Dataset::Touched affected)
{
	if (affected & Dataset::Touch::BASE) {
		markerModel.available.clear();
		if (data) {
			auto d = data->peek<Dataset::Base>();
			for (auto &id : d->protIds)
				markerModel.available.insert(id);
		}
		protList->reset();
	}

	for (auto i : {actionSplice, actionComputeHierarchy})
		i->setEnabled((bool)data);
	if (!data) {
		/* disable data-dependent actions that also depend on other things (so we don't enable) */
		for (auto i : {actionShowStructure, actionExportAnnotations, actionPersistAnnotations})
			i->setEnabled(false);
	}
}

void MainWindow::setDataset(Dataset::Ptr selected)
{
	if (data == selected)
		return;

	// disconnect from old data
	if (data)
		data->disconnect();

	// swap
	data = selected;
	if (data) {
		// let views know before our GUI might send more signals
		emit datasetSelected(data->id());
		// tell dataset what we need
		std::vector<Task> tasks; // use a pipeline to avoid redundant order computation
		if (state->annotations.id) { // simple case
			tasks.push_back({[s=state,d=data] { d->computeAnnotations(s->annotations); },
		                     Task::Type::ANNOTATE, {state->annotations.name, data->config().name}});
		} else if (state->annotations.type == Annotations::Meta::HIERCUT) {
			tasks.push_back({[s=state,d=data] { d->computeAnnotations(s->annotations); },
			                 Task::Type::PARTITION_HIERARCHY, {state->hierarchy.name, data->config().name}});
		} // note: MEANSHIFT case is handled by FAMSControl
		tasks.push_back({[s=state,d=data] { d->computeOrder(s->order); },
		                 Task::Type::ORDER, {"preference", data->config().name}});
		JobRegistry::pipeline(tasks, state->jobMonitors);
		// wire updates
		if (data)
			connect(data.get(), &Dataset::update, this, &MainWindow::updateState);
	} else {
		emit datasetDeselected();
	}

	// update own GUI state once
	updateState(Dataset::Touch::ALL);
}

void MainWindow::removeDataset(unsigned id)
{
	if (data && (data->id() == id)) // deselect
		setDataset({});
}

void MainWindow::setName(const QString &name, const QString &path)
{
	if (name.isEmpty()) {
		setWindowTitle("Belki");
		setWindowFilePath({});
	} else {
		setWindowTitle(QString("%1 – Belki").arg(name));
		setWindowFilePath(path);
	}
	actionSave->disconnect(this);
	connect(actionSave, &QAction::triggered, this, [=] { saveProject(name.isEmpty()); });
}

void MainWindow::saveProject(bool saveAs)
{
	QString filename; // optional argument
	if (saveAs) {
		filename = state->io().chooseFile(FileIO::SaveProject, this);
		if (filename.isEmpty()) // user cancel
			return;
	}
	Task task{[h=&state->hub(),filename] { h->saveProject(filename); }, Task::Type::SAVE, {}};
	JobRegistry::run(task, state->jobMonitors);
}

void MainWindow::setSelectedDataset(unsigned id)
{
	/* the whole purpose of this method is to update datasetSelect selection */

	auto model = qobject_cast<QStandardItemModel*>(datasetTree->model());

	/* we need to traverse model, no applicable finder provided by Qt */
	std::function<QModelIndex(QModelIndex)> search;
	search = [&] (QModelIndex parent) -> QModelIndex {
		for (int r = 0; r < model->rowCount(parent); ++r) {
	        auto current = model->index(r, 0, parent);
	        if (model->data(current, Qt::UserRole) == id)
				return current;
			if (model->hasChildren(current)) {
				auto index = search(current);
				if (index.isValid())
					return index;
			}
	    }
		return {};
	};
	auto index = search(model->invisibleRootItem()->index());

	/* this is a tad tricky to do due to Qt interface limitations */
	// make item current in tree to get hold of its index
	datasetTree->setCurrentIndex(index);
	// make item's parent reference point and provide index in relation to parent
	datasetSelect->setRootModelIndex(datasetTree->currentIndex().parent());
	datasetSelect->setCurrentIndex(datasetTree->currentIndex().row());
	// reset combobox to display full tree again
	datasetTree->setCurrentIndex(model->invisibleRootItem()->index());
	datasetSelect->setRootModelIndex(datasetTree->currentIndex());
}

void MainWindow::selectStructure(int id)
{
	structureSelect->setCurrentIndex(structureSelect->findData(id));

	/* clear type-dependant state */
	actionShowStructure->setEnabled(id != 0);
	actionExportAnnotations->setEnabled(id != 0);
	actionPersistAnnotations->setEnabled(false);
	toolbarActions.hierarchy->setVisible(false);
	toolbarActions.fams->setVisible(false);

	/* special items */
	if (id == 0) { // "None"
		selectAnnotations({});
		return;
	} else if (id == -1) { // Mean shift
		toolbarActions.fams->setVisible(true);
		famsControl->configure();
		famsControl->run();
		// TODO: can we avoid enabling this early?
		actionPersistAnnotations->setEnabled(true);
		return;
	}

	/* regular items */
	auto p = state->proteins().peek();
	if (p->isHierarchy((unsigned)id)) {
		auto source = std::get_if<HrClustering>(&p->structures.at(id));
		auto reasonable = source->clusters.size() / 4;
		granularitySlider->setMaximum(reasonable);
		granularitySlider->setTickInterval(reasonable / 20);
		toolbarActions.hierarchy->setVisible(true);
		actionPersistAnnotations->setEnabled(true);
		selectHierarchy(id, granularitySlider->value(), actionPruneClusters->isChecked());
	} else {
		selectAnnotations({Annotations::Meta::SIMPLE, (unsigned)id});
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
		    {Input::PROJECT, FileIO::OpenProject},
		};
		fn = state->io().chooseFile(mapping.at(type), this);
		if (fn.isEmpty())
			return; // nothing selected
	}

	auto h = &state->hub();
	auto s = h->store();
	Task task;
	switch (type) {
	case Input::DATASET:
		task = {[h,fn] { h->importDataset(fn, "Dist"); }, Task::Type::IMPORT_DATASET, {fn}};
		break;
	case Input::DATASET_RAW:
		task = {[h,fn] { h->importDataset(fn, "AbundanceLeft"); }, Task::Type::IMPORT_DATASET, {fn}};
		break;
	case Input::MARKERS:
		task = {[s,fn] { s->importMarkers(fn); }, Task::Type::IMPORT_MARKERS, {fn}};
		break;
	case Input::DESCRIPTIONS:
		task = {[s,fn] { s->importDescriptions(fn); }, Task::Type::IMPORT_DESCRIPTIONS, {fn}};
		break;
	case Input::STRUCTURE:
		if (QFileInfo(fn).suffix() == "json")
			task = {[s,fn] { s->importHierarchy(fn); }, Task::Type::IMPORT_HIERARCHY, {fn}};
		else
			task = {[s,fn] { s->importAnnotations(fn); }, Task::Type::IMPORT_ANNOTATIONS, {fn}};
		break;
	case Input::PROJECT:
		if (state->proteins().peek()->proteins.empty()) { // we are empty, so load directly
			// note: we risk that proteins gets filled while the task runs
			task = {[h,fn] { h->openProject(fn); }, Task::Type::LOAD, {fn}};
		} else {
			emit openProjectRequested(fn); // open in separate GUI, goes through guistate
		}
	}
	if (task.fun)
		JobRegistry::run(task, state->jobMonitors);
}

void MainWindow::showHelp()
{
	// TODO this leads to an awkward window format that doesn't fit on screen
	QMessageBox box(this);
	box.setWindowTitle("Help");
	box.setIcon(QMessageBox::Information);
	QFile helpText(":/help.html");
	helpText.open(QIODevice::ReadOnly);
	box.setText(helpText.readAll());
	box.setWindowModality(Qt::WindowModality::WindowModal); // sheet in OS X
	box.exec();
}

void MainWindow::selectAnnotations(const Annotations::Meta &desc)
{
	state->annotations = desc;
	emit state->annotationsChanged();
	if (state->orderSynchronizing && state->preferredOrder == Order::CLUSTERING) {
		state->order = {Order::CLUSTERING, state->annotations};
		emit state->orderChanged();
	}

	// compute if not the null case (we are not called for special cases)
	if (data && desc.id) {
		// note: prepareAnnotations in our case (types SIMPLE) always also computes order
		Task task({[s=state,d=data] { d->computeAnnotations(s->annotations); },
		           Task::Type::ANNOTATE, {desc.name, data->config().name}});
		JobRegistry::run(task, state->jobMonitors);
	}
}

void MainWindow::selectHierarchy(unsigned id, unsigned granularity, bool pruned)
{
	state->hierarchy = HrClustering::Meta{id};
	emit state->hierarchyChanged();
	switchHierarchyPartition(granularity, pruned);

	// note: the hierarchy-based order is independent of the hierarchy partition
	if (!state->orderSynchronizing ||
	    (state->preferredOrder != Order::HIERARCHY && state->preferredOrder != Order::CLUSTERING))
		return;

	state->order = {Order::HIERARCHY, state->hierarchy};
	emit state->orderChanged();
	if (data) {
		Task task{[s=state,d=data] { d->computeOrder(s->order); },
			      Task::Type::ORDER, {state->hierarchy.name, data->config().name}};
		JobRegistry::run(task, state->jobMonitors);
	}
}

void MainWindow::switchHierarchyPartition(unsigned granularity, bool pruned)
{
	state->annotations = {Annotations::Meta::HIERCUT};
	state->annotations.hierarchy = state->hierarchy.id;
	state->annotations.granularity = granularity;
	state->annotations.pruned = pruned;
	emit state->annotationsChanged();
	if (data) {
		Task task{[s=state,d=data] { d->computeAnnotations(s->annotations); },
			      Task::Type::PARTITION_HIERARCHY, {state->hierarchy.name, data->config().name}};
		JobRegistry::run(task, state->jobMonitors);
	}
}

std::optional<Annotations> MainWindow::currentAnnotations()
{
	/* maybe proteindb has it? */
	if (state->annotations.id > 0) {
		auto p = state->proteins().peek();
		auto source = std::get_if<Annotations>(&p->structures.at(state->annotations.id));
		if (source)
			return {*source}; // copy
	}

	/* ok, try to get it from data */
	if (data) {
		auto s = data->peek<Dataset::Structure>();
		auto source = s->fetch(state->annotations);
		if (source)
			return {*source}; // copy
	}
	return {};
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
	event->setDropAction(Qt::DropAction::CopyAction);
	auto urls = event->mimeData()->urls();
	if (urls.empty())
		return;

	/* Accept a single project file drop */
	if (urls.first().toLocalFile().endsWith(".belki", Qt::CaseInsensitive)) {
		if (urls.size() != 1) // more than one project file? don't even bother
			return; // do not accept event
		emit openProjectRequested(urls.first().toLocalFile());
		event->accept();
		return;
	}

	auto title = (urls.size() == 1 ? "Load file as…"
	                               : QString("Load %1 files as…").arg(urls.size()));

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

	event->accept();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	// delegate to signal handler, who might decide the window stays
	event->ignore();
	emit closeWindowRequested();
}

bool MainWindow::CustomShowAndEnableProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
	if (!onlyMarkers)
		return true;

	auto checkState = sourceModel()->data(sourceModel()->index(row, 0, parent), Qt::CheckStateRole);
	return checkState != Qt::Unchecked;
}

Qt::ItemFlags MainWindow::CustomShowAndEnableProxyModel::flags(const QModelIndex &index) const
{
	auto flags = sourceModel()->flags(mapToSource(index));
	flags.setFlag(Qt::ItemFlag::ItemIsEnabled,
	              available.empty() ? false :
	                                  available.count(data(index, Qt::UserRole + 1).toInt()));
	return flags;
}
