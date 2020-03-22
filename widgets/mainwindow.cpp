#include "mainwindow.h"
#include "guistate.h"
#include "windowstate.h"
#include "datahub.h"
#include "storage/storage.h"
#include "fileio.h"
#include "profiles/profilewindow.h"
#include "widgets/spawndialog.h"

#include "scatterplot/dimredtab.h"
#include "scatterplot/scattertab.h"
#include "heatmap/heatmaptab.h"
#include "distmat/distmattab.h"
#include "profiles/profiletab.h"
#include "profiles/bnmstab.h"
#include "featweights/featweightstab.h"

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
#include <QtConcurrent>
#include <QDateTime>

MainWindow::MainWindow(std::shared_ptr<WindowState> state) :
    state(state)
{
	setupUi(this);
	profiles->setState(state);
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

	// initialize widgets to be empty & most-restrictive
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

	/** Datasets **/
	// setup datasets selection model+view
	datasetTree = new QTreeView(this);
	datasetTree->setHeaderHidden(true);
	datasetTree->setFrameShape(QFrame::Shape::NoFrame);
	datasetTree->setSelectionMode(QTreeView::SelectionMode::NoSelection);
	datasetTree->setItemsExpandable(false);
	datasetSelect->setView(datasetTree);
}

void MainWindow::setupToolbar()
{
	// put datasets and some space before partition area
	auto anchor = actionShowStructure;
	toolBar->insertWidget(anchor, datasetLabel);
	toolbarActions.datasets = toolBar->insertWidget(anchor, datasetSelect);
	toolBar->insertSeparator(anchor);

	// fill-up partition area
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
		switchHierarchyPartition((unsigned)v);
	});
	connect(famsKSlider, &QSlider::valueChanged, [this] (int v) {
		float k = v*0.01f;
		famsKSlider->setToolTip(QString("Parameter k: %1").arg(k));
		selectFAMS(k);
	});
}

void MainWindow::setupActions()
{
	/* Shortcuts (standard keys not available in UI Designer) */
	actionOpenProject->setShortcut(QKeySequence::StandardKey::Open);
	actionSave->setShortcut(QKeySequence::StandardKey::Save);
	actionSaveAs->setShortcut(QKeySequence::StandardKey::SaveAs);
	actionCloseProject->setShortcut(QKeySequence::StandardKey::Close);
	actionHelp->setShortcut(QKeySequence::StandardKey::HelpContents);
	actionQuit->setShortcut(QKeySequence::StandardKey::Quit);

	/* Buttons to be wired to actions */
	loadMarkersButton->setDefaultAction(actionLoadMarkers);
	saveMarkersButton->setDefaultAction(actionSaveMarkers);
	clearMarkersButton->setDefaultAction(actionClearMarkers);

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

		runInBackground([s=state->hub().store(),filename] { s->exportMarkers(filename); });
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

		// TODO we cannot move unique_ptr to other thread. so no bg
		state->hub().store()->exportAnnotations(filename, *localCopy);
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
		// TODO we cannot move unique_ptr to other thread. so no bg
		state->proteins().addAnnotations(std::move(localCopy), false, true);
	});
	connect(actionClearMarkers, &QAction::triggered, &state->proteins(), &ProteinDB::clearMarkers);

	connect(actionSplice, &QAction::triggered, [this] {
		if (!data)
			return;
		auto s = new SpawnDialog(data, state, this);
		// spawn dialog deletes itself, should also kill connection+lambda, right?
		connect(s, &SpawnDialog::spawn, [this] (auto data, auto& config) {
			state->hub().spawn(data, config);
		});
	});

	connect(actionSave, &QAction::triggered, [this] { state->hub().saveProject(); });
	connect(actionSaveAs, &QAction::triggered, [this] {
		auto filename = state->io().chooseFile(FileIO::SaveProject, this);
		if (filename.isEmpty())
			return;
		state->hub().saveProject(filename);
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

	// connect singnalling into view (TODO: they should connect themselves)
	auto hub = &state->hub();
	connect(hub, &DataHub::newDataset, v, &Viewer::addDataset);
	/* use queued conn. to ensure the views get the newDataset signal _first_! */
	connect(this, &MainWindow::datasetSelected, v, &Viewer::selectDataset, Qt::QueuedConnection);

	// connect signalling out of view
	connect(v, &Viewer::markerToggled, this, &MainWindow::markerToggled);
	connect(v, &Viewer::proteinsHighlighted, profiles, &ProfileWidget::updateDisplay);

	auto renderSlot = [this] (auto r, auto d) {
		auto title = (data ? data->config().name : windowTitle());
		state->io().renderToFile(r, {title, d});
	};
	connect(v, qOverload<QGraphicsView*, QString>(&Viewer::exportRequested), renderSlot);
	connect(v, qOverload<QGraphicsScene*, QString>(&Viewer::exportRequested), renderSlot);

	// TODO: they could do that themselves, too
	for (auto &[_, d] : hub->datasets())
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
	if (affected & Dataset::Touch::BASE) {
		markerModel.available.clear();
		if (data) {
			auto d = data->peek<Dataset::Base>();
			for (auto &id : d->protIds)
				markerModel.available.insert(id);
		}
		protList->reset();
	}

	if (!data) {
		/* hide and disable widgets that need data or even more */
		actionSplice->setEnabled(false);
		actionShowStructure->setEnabled(false);
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setEnabled(false);
		actionPersistAnnotations->setEnabled(false);
		return;
	}

	/* re-enable actions that depend only on data */
	actionSplice->setEnabled(true);
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
		emit datasetSelected(data ? data->id() : 0);
		// tell dataset what we need
		runOnData([=] (auto d) {
			// one thread as work might be redundant
			d->prepareAnnotations(state->annotations);
			d->prepareOrder(state->order);
		});
		// wire updates
		if (data)
			connect(data.get(), &Dataset::update, this, &MainWindow::updateState);
	}

	// update own GUI state once
	updateState(Dataset::Touch::ALL);
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
	actionSave->setDisabled(name.isEmpty());
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
	toolbarActions.granularity->setVisible(false);
	toolbarActions.famsK->setVisible(false);

	/* special items */
	if (id == 0) { // "None"
		selectAnnotations({});
		return;
	} else if (id == -1) { // Mean shift
		selectFAMS(famsKSlider->value()*0.01f);
		toolbarActions.famsK->setVisible(true);
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
		toolbarActions.granularity->setVisible(true);
		actionPersistAnnotations->setEnabled(true);
		selectHierarchy(id, granularitySlider->value());
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

	auto &hub = state->hub();
	auto s = hub.store();
	switch (type) {
	case Input::DATASET:      hub.importDataset(fn, "Dist"); break;
	case Input::DATASET_RAW:  hub.importDataset(fn, "AbundanceLeft"); break;
	case Input::MARKERS:      runInBackground([s,fn] { s->importMarkers(fn); }); break;
	case Input::DESCRIPTIONS: runInBackground([s,fn] { s->importDescriptions(fn); }); break;
	case Input::STRUCTURE: {
		if (QFileInfo(fn).suffix() == "json")
			runInBackground([s,fn] { s->importHierarchy(fn); });
		else
			runInBackground([s,fn] { s->importAnnotations(fn); });
		break;
	}
	case Input::PROJECT: emit openProjectRequested(fn); // goes through guistate
	}
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
	runOnData([=] (auto d) { d->prepareAnnotations(state->annotations); });

	if (!state->orderSynchronizing || state->preferredOrder != Order::CLUSTERING)
		return;

	state->order = {Order::CLUSTERING, state->annotations};
	emit state->orderChanged();
	runOnData([=] (auto d) { d->prepareOrder(state->order); });
}

void MainWindow::selectFAMS(float k)
{
	Annotations::Meta desc{Annotations::Meta::MEANSHIFT};
	desc.k = k;
	selectAnnotations(desc);
}

void MainWindow::selectHierarchy(unsigned id, unsigned granularity)
{
	state->hierarchy = HrClustering::Meta{id};
	emit state->hierarchyChanged();
	switchHierarchyPartition(granularity);

	if (!state->orderSynchronizing ||
	    (state->preferredOrder != Order::HIERARCHY && state->preferredOrder != Order::CLUSTERING))
		return;

	state->order = {Order::HIERARCHY, state->hierarchy};
	emit state->orderChanged();
	runOnData([=] (auto d) { d->prepareOrder(state->order); });
}

void MainWindow::switchHierarchyPartition(unsigned granularity)
{
	state->annotations = {Annotations::Meta::HIERCUT};
	state->annotations.hierarchy = state->hierarchy.id;
	state->annotations.granularity = granularity;
	emit state->annotationsChanged();
	runOnData([=] (auto d) { d->prepareAnnotations(state->annotations); });
}

std::unique_ptr<Annotations> MainWindow::currentAnnotations()
{
	/* maybe proteindb has it? */
	if (state->annotations.id > 0) {
		auto p = state->proteins().peek();
		auto source = std::get_if<Annotations>(&p->structures.at(state->annotations.id));
		if (source)
			return std::make_unique<Annotations>(*source);
	}

	/* ok, try to get it from data */
	if (data) {
		auto s = data->peek<Dataset::Structure>();
		auto source = s->fetch(state->annotations);
		if (source) // always return a copy, the pointer is guarded by RAII!
			return std::make_unique<Annotations>(*source);
	}

	return {};
}

void MainWindow::runInBackground(const std::function<void()> &work)
{
	QtConcurrent::run(work);
}

void MainWindow::runOnData(const std::function<void(Dataset::Ptr)> &work)
{
	if (!data)
		return;
	// pass shared ptr by value so thread works on own copy
	runInBackground([target=data,work] { work(target); });
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

Qt::ItemFlags MainWindow::CustomEnableProxyModel::flags(const QModelIndex &index) const
{
	auto flags = sourceModel()->flags(mapToSource(index));
	flags.setFlag(Qt::ItemFlag::ItemIsEnabled,
	              available.empty() ? false :
	                                  available.count(data(index, Qt::UserRole + 1).toInt()));
	return flags;
}
