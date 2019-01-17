#include "mainwindow.h"
#include "dataset.h"
#include "chart.h"
#include "heatmapscene.h"
#include "distmatscene.h"
#include "profilechart.h"
#include "profilewindow.h"

#include <QFileInfo>
#include <QDir>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>

#include <QtDebug>

constexpr auto hierarchyPostfix = " (Hierarchy)";
const QVector<QColor> tableau20 = {
    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), store(data),
    chart(new Chart(data)),
    heatmap(new HeatmapScene(data)),
    distmat(new DistmatScene(data)),
    cursorChart(new ProfileChart),
    fileLabel(new QLabel),
    io(new FileIO(this))
{
	store.moveToThread(&dataThread);
	data.moveToThread(&dataThread);
	dataThread.start();

	setupUi(this);
	setupToolbar();

	/* main chart */
	chartView->setChart(chart);
	chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* heatmap */
	heatmapView->setScene(heatmap);

	/* Distance matrix */
	distmatView->setScene(distmat);

	/* cursor chart */
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	// common background for plot and its container
	auto p = cursorInlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	cursorInlet->setPalette(p);

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
	// put stuff before other buttons
	fileLabel->setText("<i>No file selected</i>");
	toolBar->insertWidget(actionLoadDescriptions, fileLabel);

	auto anchor = actionComputeDisplay;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);
	anchor = actionLoadAnnotations;
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, partitionLabel);

	toolbarActions.partitions = toolBar->insertWidget(anchor, partitionSelect);
	toolbarActions.granularity = toolBar->insertWidget(actionExportAnnotations, granularitySlider);
	toolbarActions.famsK = toolBar->insertWidget(actionExportAnnotations, famsKSlider);

	// right-align screenshot & help button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

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
	connect(&data, &Dataset::newSource, this, &MainWindow::resetData);
	connect(&data, &Dataset::newDisplay, this, &MainWindow::updateData);
	connect(&data, &Dataset::newClustering, this, [this] {
		chart->clearPartitions();
		chart->updatePartitions();
		heatmap->recolor();
		distmat->reorder();
		actionShowPartition->setEnabled(true);
		actionShowPartition->setChecked(true);
	});
	connect(&data, &Dataset::newHierarchy, this, [this] {
		auto d = data.peek();
		auto reasonable = std::min(d->proteins.size(), d->hierarchy.size()) / 4;
		granularitySlider->setMaximum(reasonable);
		distmat->reorder();
	});

	/* notifications from views */
	connect(chart, &Chart::cursorChanged, this, &MainWindow::updateCursorList);
	connect(heatmap, &HeatmapScene::cursorChanged, this, &MainWindow::updateCursorList);
	connect(distmat, &DistmatScene::cursorChanged, this, &MainWindow::updateCursorList);

	/* signals for designated slots (for thread-affinity) */
	connect(this, &MainWindow::openDataset, &store, &Storage::openDataset);
	connect(this, &MainWindow::readAnnotations, &store, &Storage::readAnnotations);
	connect(this, &MainWindow::readHierarchy, &store, &Storage::readHierarchy);
	connect(this, &MainWindow::importDescriptions, &store, &Storage::importDescriptions);
	connect(this, &MainWindow::importAnnotations, &store, &Storage::importAnnotations);
	connect(this, &MainWindow::importHierarchy, &store, &Storage::importHierarchy);
	connect(this, &MainWindow::exportAnnotations, &store, &Storage::exportAnnotations);
	connect(this, &MainWindow::computeDisplay, &data, &Dataset::computeDisplay);
	connect(this, &MainWindow::calculatePartition, &data, &Dataset::calculatePartition);
	connect(this, &MainWindow::runFAMS, &data, &Dataset::computeFAMS);
	qRegisterMetaType<QVector<QColor>>();
	connect(this, &MainWindow::updateColorset, &data, &Dataset::updateColorset);
	connect(this, &MainWindow::updateColorset, chart, &Chart::updateColorset);

	/* selecting display/partition/etc. always goes through GUI */
	connect(transformSelect, &QComboBox::currentTextChanged, [this] (auto name) {
		if (name.isEmpty())
			return;
		chart->display(name);
		chartView->setEnabled(true);
	});
	connect(partitionSelect, QOverload<int>::of(&QComboBox::activated), [this] () {
		// clear partition-type dependant state
		toolbarActions.granularity->setVisible(false);
		toolbarActions.famsK->setVisible(false);
		actionExportAnnotations->setVisible(false);
		actionShowPartition->setEnabled(true);

		// special items (TODO: better use an enum here, maybe include hierarchies)
		if (partitionSelect->currentData().isValid()) {

			auto v = partitionSelect->currentData().value<int>();
			if (v == 0) {
				actionShowPartition->setChecked(false);
				actionShowPartition->setEnabled(false);
				data.cancelFAMS();
				// TODO: better to remove annotation instead of hiding it
			} else if (v == 1) {
				data.changeFAMS((unsigned)famsKSlider->value() * 0.01f);
				emit runFAMS();
				toolbarActions.famsK->setVisible(true);
				actionExportAnnotations->setVisible(true);
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
			actionExportAnnotations->setVisible(true);
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

	/* Buttons to be wired to actions */
	loadMarkersButton->setDefaultAction(actionLoadMarkers);
	saveMarkersButton->setDefaultAction(actionSaveMarkers);
	clearMarkersButton->setDefaultAction(actionClearMarkers);
	profileViewButton->setDefaultAction(actionProfileView);

	connect(actionHelp, &QAction::triggered, this, &MainWindow::showHelp);
	connect(actionLoadDataset, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenDataset);
		if (filename.isEmpty())
			return;
		fileLabel->setText(QString("<i>Calculating…</i>"));
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
	connect(actionShowPartition, &QAction::toggled, chart, &Chart::togglePartitions);
	connect(actionLoadMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenMarkers);
		if (filename.isEmpty())
			return;
		for (auto i : store.importMarkers(filename))
			chart->addMarker(i);
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
	connect(actionClearMarkers, &QAction::triggered, chart, &Chart::clearMarkers);
	connect(actionSavePlot, &QAction::triggered, [this] {
		io->renderToFile(chartView, {title, transformSelect->currentText()});
	});

	connect(actionProfileView, &QAction::triggered, [this] {
		new ProfileWindow(cursorChart, this);
	});

	connect(actionComputeDisplay, &QAction::triggered, [this] {
		auto methods = dimred::availableMethods();
		auto menu = new QMenu(this);
		for (const auto& m : methods) {
			if (transformSelect->findText(m.id) >= 0)
				continue;

			menu->addAction(m.description, [this, m] { emit computeDisplay(m.name); });
		}
		menu->popup(QCursor::pos());
	});
}

void MainWindow::setupMarkerControls()
{
	/* setup completer with empty model */
	auto m = new QStandardItemModel;
	auto cpl = new QCompleter(m);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	// we expect model entries to be sorted
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	cpl->setCompletionMode(QCompleter::InlineCompletion);
	protSearch->setCompleter(cpl);
	protList->setModel(cpl->completionModel());

	/* synchronize with chart */
	connect(chart, &Chart::markerToggled, [this] (unsigned idx, bool present) {
		this->markerItems[idx]->setCheckState(present ? Qt::Checked : Qt::Unchecked);
	});
	connect(chart, &Chart::markersCleared, [this] () {
		for (auto m : qAsConst(this->markerItems))
			m->setCheckState(Qt::Unchecked);
	});
	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		if (i->checkState() == Qt::Checked)
			chart->addMarker((unsigned)i->data().toInt());
		if (i->checkState() == Qt::Unchecked)
			chart->removeMarker((unsigned)i->data().toInt());
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
	/* no displays */
	transformSelect->clear();

	/* no partitions except inbuilt mean-shift */
	partitionSelect->clear();
	partitionSelect->addItem("None", {0});
	partitionSelect->addItem("Adaptive Mean Shift", {1});
	// TODO

	/* hide and disable widgets that need data or even more */
	actionShowPartition->setChecked(false);
	actionShowPartition->setEnabled(false);
	toolbarActions.granularity->setVisible(false);
	toolbarActions.famsK->setVisible(false);
	actionExportAnnotations->setVisible(false);

	chart->clear();
	chartView->setEnabled(false); // TODO: can markerWidget crash uninit. chartView?
	heatmap->reset();
	heatmapView->setEnabled(false);
	distmat->reset();
	distmatView->setEnabled(false);
}

void MainWindow::resetData()
{
	clearData();

	title = store.name();
	setWindowTitle(QString("%1 – Belki").arg(title));
	fileLabel->setText(QString("<b>%1</b>").arg(title));

	/* set up cursor chart */
	cursorChart->setCategories(data.peek()->dimensions);

	/* set up marker controls */
	updateMarkerControls();

	/* set up heatmap+distmat (chart will set-up when new display comes available */
	heatmap->reset(true);
	heatmapView->setEnabled(true);
	distmat->reset(true);
	distmatView->setEnabled(true);
}

void MainWindow::updateData(const QString &display)
{
	transformSelect->addItem(display); // duplicates ignored
	transformSelect->setCurrentText(display);
}

void MainWindow::updateCursorList(QVector<unsigned> samples, QString title)
{
	/* clear plot */
	cursorChart->setTitle(title);
	cursorChart->clear();
	if (samples.empty()) {
		cursorList->clear();
		// only change title label to avoid geometry change under Windows
		cursorCaption->setDisabled(true);
		actionProfileView->setDisabled(true);
		return;
	}

	auto d = data.peek();

	/* set up plot */
	for (auto i : qAsConst(samples))
		cursorChart->addSample(d->proteins[i].name, d->featurePoints[i]);
	cursorChart->finalize();

	/* set up list */

	// reduce set
	const int showMax = 25;
	auto text = QString("%1");
	if (samples.size() > showMax) {
		text.append(QString("… (%1 total)").arg(samples.size()));
		samples.resize(showMax - 1);
	}
	// sort by name -- _after_ set reduction to get a broad representation
	std::sort(samples.begin(), samples.end(), [&d] (const unsigned& a, const unsigned& b) {
		return d->proteins[a].name < d->proteins[b].name;
	});
	// compose list
	QString content;
	QString tpl("<b><a href='https://uniprot.org/uniprot/%1_%2'>%1</a></b> <small>%3 <i>%4</i></small><br>");
	for (auto i : qAsConst(samples)) {
		auto &p = d->proteins[i];
		auto clusters = std::accumulate(p.memberOf.begin(), p.memberOf.end(), QStringList(),
		    [&d] (QStringList a, unsigned b) { return a << d->clustering[b].name; });
		content.append(tpl.arg(p.name, p.species, clusters.join(", "), p.description));
	}
	cursorList->setText(text.arg(content));

	cursorCaption->setEnabled(true);
	actionProfileView->setEnabled(true);
}

void MainWindow::updateMarkerControls()
{
	/* re-fill model */
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->clear();
	markerItems.clear();
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

	markerWidget->setEnabled(true);
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
