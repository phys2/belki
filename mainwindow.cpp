#include "mainwindow.h"
#include "dataset.h"
#include "widgets/profilechart.h"
#include "widgets/profilewindow.h"

#include <QFileInfo>
#include <QDir>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QLabel>
#include <QMessageBox>

#include <random>

constexpr auto hierarchyPostfix = " (Hierarchy)";
const QVector<QColor> tableau20 = {
    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), store(data),
    cursorChart(new ProfileChart(data)),
    io(new FileIO(this))
{
	store.moveToThread(&dataThread);
	data.moveToThread(&dataThread);
	dataThread.start();

	setupUi(this);
	setupToolbar();

	/* Views in tabs */
	views = {dimredTab, scatterTab, heatmapTab, distmatTab, featweightsTab};
	for (auto v : views) {
		v->init(&data);
		// connect singnalling into view
		connect(this, &MainWindow::updateColorset, v, &Viewer::inUpdateColorset);
		connect(this, &MainWindow::reset, v, &Viewer::inReset);
		connect(this, &MainWindow::repartition, v, &Viewer::inRepartition);
		connect(this, &MainWindow::reorder, v, &Viewer::inReorder);
		connect(this, &MainWindow::toggleMarker, v, &Viewer::inToggleMarker);
		connect(this, &MainWindow::togglePartitions, v, &Viewer::inTogglePartitions);

		// connect signalling out of view
		connect(v, &Viewer::markerToggled, [this] (unsigned idx, bool present) {
			this->markerItems[idx]->setCheckState(present ? Qt::Checked : Qt::Unchecked);
		});
		connect(v, &Viewer::cursorChanged, this, &MainWindow::updateCursorList);
		auto renderSlot = [this] (auto r, auto d) {
			io->renderToFile(r, {title, d});
		};
		connect(v, &Viewer::orderRequested, &data, &Dataset::changeOrder);
		connect(v, qOverload<QGraphicsView*, QString>(&Viewer::exportRequested), renderSlot);
		connect(v, qOverload<QGraphicsScene*, QString>(&Viewer::exportRequested), renderSlot);

		// gui synchronization between views
		for (auto v2 : views) {
			if (v2 == v)
				continue;
			connect(v, &Viewer::orderRequested, v2, &Viewer::changeOrder);
		}
	}

	/* cursor chart */
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	// common background for plot and its container
	auto p = cursorInlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	cursorInlet->setPalette(p);
	// move button into chart (evil :D)
	profileViewButton->setParent(cursorPlot);
	profileViewButton->move(4, 4);
	cursorTopBar->deleteLater();

	setupMarkerControls();
	setupSignals(); // after setupToolbar(), signal handlers rely on initialized actions
	setupActions();

	emit updateColorset(tableau20);

	// initialize widgets to be empty & most-restrictive
	clearData();
}

MainWindow::~MainWindow()
{
	dataThread.quit();
	dataThread.wait();
}

void MainWindow::setupToolbar()
{
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
	/** signals **/
	/* error dialogs */
	connect(&store, &Storage::ioError, this, &MainWindow::displayError);
	connect(&data, &Dataset::ioError, this, &MainWindow::displayError);
	connect(io, &FileIO::ioError, this, &MainWindow::displayError);

	/* notifications from data/storage thread */
	connect(&store, &Storage::newAnnotations, this, [this] (auto name, bool loaded) {
		if (loaded) { // already pre-selected, need to reflect that
			QSignalBlocker _(partitionSelect);
			partitionSelect->addItem(name);
			partitionSelect->setCurrentText(name);
		} else {
			partitionSelect->addItem(name);
		}
	});
	connect(&store, &Storage::newHierarchy, this, [this] (auto name, bool loaded) {
		auto n = name + hierarchyPostfix;
		partitionSelect->addItem(n);
		if (loaded) { // already pre-selected, need to reflect that
			QSignalBlocker _(partitionSelect);
			partitionSelect->setCurrentText(n);
		}
	});

	connect(&data, &Dataset::selectedDataset, this, &MainWindow::resetData);
	connect(&data, &Dataset::newDataset, this, &MainWindow::newData);
	connect(&data, &Dataset::newClustering, this, [this] (bool withOrder) {
		bool haveClustering = !(data.peek()->clustering.empty());
		actionShowPartition->setEnabled(haveClustering);
		actionShowPartition->setChecked(haveClustering);
		emit repartition(withOrder);
	});
	connect(&data, &Dataset::newHierarchy, this, [this] (bool withOrder) {
		auto d = data.peek();
		auto reasonable = std::min(d->proteins.size(), d->hierarchy.size()) / 4;
		granularitySlider->setMaximum(reasonable);
		if (withOrder)
			emit reorder();
	});
	connect(&data, &Dataset::newOrder, this, &MainWindow::reorder);

	/* signals for designated slots (for thread-affinity) */
	connect(this, &MainWindow::openDataset, &store, &Storage::openDataset);
	connect(this, &MainWindow::readAnnotations, &store, &Storage::readAnnotations);
	connect(this, &MainWindow::readHierarchy, &store, &Storage::readHierarchy);
	connect(this, &MainWindow::importDescriptions, &store, &Storage::importDescriptions);
	connect(this, &MainWindow::importAnnotations, &store, &Storage::importAnnotations);
	connect(this, &MainWindow::importHierarchy, &store, &Storage::importHierarchy);
	connect(this, &MainWindow::exportAnnotations, &store, &Storage::exportAnnotations);
	connect(this, &MainWindow::selectDataset, &data, &Dataset::select);
	connect(this, &MainWindow::clearClusters, &data, &Dataset::clearClusters);
	connect(this, &MainWindow::calculatePartition, &data, &Dataset::calculatePartition);
	connect(this, &MainWindow::runFAMS, &data, &Dataset::computeFAMS);
	connect(this, &MainWindow::updateColorset, &data, &Dataset::updateColorset);

	/* selecting dataset */
	connect(datasetSelect, QOverload<int>::of(&QComboBox::activated), [this] {
		emit selectDataset((unsigned)datasetSelect->currentData().toInt());
	});

	/* selecting/altering partition */
	connect(partitionSelect, QOverload<int>::of(&QComboBox::activated), [this] {
		// clear partition-type dependant state
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setEnabled(false);

		// special items (TODO: better use an enum here, maybe include hierarchies)
		if (partitionSelect->currentData().isValid()) {

			auto v = partitionSelect->currentData().value<int>();
			if (v == 0) {
				data.cancelFAMS();
				emit clearClusters();
			} else if (v == 1) {
				data.changeFAMS((unsigned)famsKSlider->value() * 0.01f);
				emit runFAMS();
				toolbarActions.famsK->setVisible(true);
				actionExportAnnotations->setEnabled(true);
			}
			return;
		}

		// not FAMS? cancel it in case it is running
		data.cancelFAMS();

		// regular items: identified by name
		auto name = partitionSelect->currentText();
		if (name.isEmpty())
			return;

		bool isHierarchy = name.endsWith(hierarchyPostfix);
		if (isHierarchy) {
			auto n = name.chopped(strlen(hierarchyPostfix));
			emit readHierarchy(n);
			emit calculatePartition((unsigned)granularitySlider->value());
			toolbarActions.granularity->setVisible(true);
			actionExportAnnotations->setEnabled(true);
		} else {
			emit readAnnotations(name);
		}
	});
	connect(granularitySlider, &QSlider::valueChanged, this, &MainWindow::calculatePartition);
	connect(famsKSlider, &QSlider::valueChanged, [this] (int v) {
		data.changeFAMS(v * 0.01f); // reconfigure from outside (this thread)
		emit runFAMS(); // start calculation inside data thread
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
	profileViewButton->setDefaultAction(actionProfileView);

	connect(actionQuit, &QAction::triggered, [] { QApplication::exit(); });
	connect(actionHelp, &QAction::triggered, this, &MainWindow::showHelp);
	connect(actionLoadDataset, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenDataset);
		if (filename.isEmpty())
			return;
		// avoid queueing signals from widgets referencing old data
		clearData();
		emit openDataset(filename);
	});
	connect(actionLoadDescriptions, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenDescriptions);
		if (filename.isEmpty())
			return;
		emit importDescriptions(filename);
	});
	connect(actionLoadAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenClustering);
		if (filename.isEmpty())
			return;
		auto filetype = QFileInfo(filename).suffix();
		if (filetype == "json")
			emit importHierarchy(filename);
		else
			emit importAnnotations(filename);
	});
	connect(actionExportAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveAnnotations);
		if (filename.isEmpty())
			return;

		emit exportAnnotations(filename);
	});
	connect(actionShowPartition, &QAction::toggled, this, &MainWindow::togglePartitions);
	connect(actionClearMarkers, &QAction::triggered, [this] {
		for (auto i : qAsConst(this->markerItems))
			i->setCheckState(Qt::Unchecked);
	});
	connect(actionLoadMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenMarkers);
		if (filename.isEmpty())
			return;
		for (auto i : store.importMarkers(filename)) {
			this->markerItems[i]->setCheckState(Qt::Checked);
		}
	});
	connect(actionSaveMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::SaveMarkers);
		if (filename.isEmpty())
			return;
		QVector<unsigned> indices;
		for (auto m : qAsConst(this->markerItems))
			if (m->checkState() == Qt::Checked)
				indices.append((unsigned)m->data().toInt());
		store.exportMarkers(filename, indices);
	});

	connect(actionProfileView, &QAction::triggered, [this] {
		new ProfileWindow(cursorChart, this);
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
		auto index = (unsigned)i->data().toInt();
		emit toggleMarker(index, i->checkState() == Qt::Checked);
	});

	auto toggler = [m] (QModelIndex i) {
		if (!i.isValid())
			return; // didn't click on a row, e.g. clicked on a checkmark
		auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
		if (!proxy)
			return; // sorry, can't do this!
		auto item = m->itemFromIndex(proxy->mapToSource(i));
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

void MainWindow::clearData()
{
	// TODO: if we do a hard reset we would have to do this…
	/* does not work with the current call order in newData()
	datasetSelect.clear();
	toolbarActions.datasets->setEnabled(false);
	*/
	setFilename({}); // TODO belongs to hard reset

	/* reset partitions: none except inbuilt mean-shift */
	partitionSelect->clear();
	partitionSelect->addItem("None", {0});
	partitionSelect->addItem("Adaptive Mean Shift", {1});

	/* hide and disable widgets that need data or even more */
	toolbarActions.partitions->setEnabled(false);
	actionShowPartition->setChecked(false);
	actionShowPartition->setEnabled(false);
	toolbarActions.granularity->setVisible(false);
	toolbarActions.famsK->setVisible(false);
	actionExportAnnotations->setEnabled(false);

	/* reset views first (before our widgets emit signals) */
	emit reset(false);

	/* clear out markers TODO issue #18 */
	resetMarkerControls();
}

void MainWindow::resetData()
{
	clearData();

	/* reset views first (before our widgets emit signals) */
	emit reset(true);

	/* set up cursor chart */
	cursorChart->setCategories(data.peek()->dimensions);

	/* set up marker controls */
	resetMarkerControls();

	/* re-enable actions that depend only on data */
	toolbarActions.partitions->setEnabled(true);
}

void MainWindow::newData(unsigned index)
{
	// TODO wronge place to do this in new storage concept
	setFilename(store.name());

	/* add to datasets */
	auto name = data.peek()->conf.name;
	datasetSelect->addItem(name, index);
	datasetSelect->setCurrentText(name);
	toolbarActions.datasets->setEnabled(true);

	/* re-init everything */
	resetData();
}

void MainWindow::updateCursorList(QVector<unsigned> samples, QString title)
{
	/* clear plot */
	cursorChart->setTitle(title);
	cursorChart->clear();
	if (samples.empty()) {
		cursorList->clear();
		cursorWidget->setDisabled(true);
		actionProfileView->setDisabled(true);
		return;
	}

	/* determine marker proteins contained in samples */
	std::set<unsigned> markers;
	for (auto i : qAsConst(samples)) {
		if (markerItems.contains(i) && markerItems[i]->checkState() == Qt::Checked)
			markers.insert(i);
	}

	/* set up plot */
	for (auto i : qAsConst(samples))
		cursorChart->addSample(i, markers.count(i));
	cursorChart->finalize();

	/* set up list */

	// determine how many lines we can fit
	auto total = samples.size();
	auto testFont = cursorList->currentFont(); // replicate link font
	testFont.setBold(true);
	testFont.setUnderline(true);
	auto showMax = cursorList->contentsRect().height() /
	        QFontMetrics(testFont).lineSpacing() - 1;

	// create format string and reduce set
	auto text = QString("%1");
	if (total > showMax) {
		text.append("… ");
		// shuffle before cutting off
		std::shuffle(samples.begin(), samples.end(), std::mt19937(0));
		samples.resize(showMax);
	}
	text.append(QString("(%1 total)").arg(total));

	// sort by name -- _after_ set reduction to get a broad representation
	auto d = data.peek();
	std::sort(samples.begin(), samples.end(), [&d] (const unsigned& a, const unsigned& b) {
		return d->proteins[a].name < d->proteins[b].name;
	});
	// compose list
	QString content;
	QString tpl("<b><a href='https://uniprot.org/uniprot/%1_%2'>%1</a></b> <small>%3 <i>%4</i></small><br>");
	for (auto i : qAsConst(samples)) {
		 // highlight marker proteins
		if (markers.count(i))
			content.append("<small>★</small>");
		auto &p = d->proteins[i];
		auto &m = d->clustering.memberships[i];
		auto clusters = std::accumulate(m.begin(), m.end(), QStringList(),
		    [&d] (QStringList a, unsigned b) { return a << d->clustering.clusters.at(b).name; });
		content.append(tpl.arg(p.name, p.species, clusters.join(", "), p.description));
	}
	cursorList->setText(text.arg(content));

	cursorWidget->setEnabled(true);
	actionProfileView->setEnabled(true);
}

void MainWindow::resetMarkerControls()
{
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	/* clear model */
	m->clear();
	markerItems.clear();

	/* re-fill model */
	auto d = data.peek();
	for (auto i : d->protIndex) { // use index to have it sorted (required!)
		auto item = new QStandardItem;
		item->setText(d->proteins[i.second].name);
		item->setData(i.second);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		m->appendRow(item);
		markerItems[i.second] = item;
	}

	/* enable if we have data */
	markerWidget->setEnabled(!markerItems.isEmpty());
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
