#include "mainwindow.h"
#include "dataset.h"
#include "chart.h"
#include "profilechart.h"
#include "profilewindow.h"

#include <QFileInfo>
#include <QStandardItemModel>
#include <QDir>
#include <QCompleter>
#include <QAbstractProxyModel>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), chart(new Chart(data)), cursorChart(new ProfileChart),
    fileLabel(new QLabel),
    io(new FileIO(this))
{
	data.moveToThread(&dataThread);
	dataThread.start();

	setupUi(this);
	setupToolbar();

	/* main chart */
	chartView->setChart(chart);
	chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* cursor chart */
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	// common background for plot and its container
	auto p = cursorInlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	cursorInlet->setPalette(p);

	/* marker controls */
	setupMarkerControls();

	/* signals */
	connect(&data, &Dataset::ioError, this, &MainWindow::displayError);
	connect(io, &FileIO::ioError, this, &MainWindow::displayError);

	connect(this, &MainWindow::loadDataset, &data, &Dataset::loadDataset);
	connect(this, &MainWindow::loadAnnotations, &data, &Dataset::loadAnnotations);
	connect(this, &MainWindow::loadHierarchy, &data, &Dataset::loadHierarchy);
	connect(this, &MainWindow::calculatePartition, &data, &Dataset::calculatePartition);
	connect(&data, &Dataset::newData, this, &MainWindow::updateData);
	connect(&data, &Dataset::newClustering, this, [this] {
		chart->updatePartitions(true);
		actionShowPartition->setEnabled(true);
		actionShowPartition->setChecked(true);
	});
	connect(&data, &Dataset::newHierarchy, this, [this] {
		emit calculatePartition((unsigned)granularitySlider->value());
	});

	connect(chart, &Chart::cursorChanged, this, &MainWindow::updateCursorList);

	connect(transformSelect, qOverload<const QString&>(&QComboBox::currentIndexChanged),
	        [this] (const QString &name) { chart->display(name); });
	connect(granularitySlider, &QSlider::valueChanged, this, &MainWindow::calculatePartition);

	/* actions */
	setupActions();
}

MainWindow::~MainWindow()
{
	dataThread.quit();
	dataThread.wait();
}

void MainWindow::setupToolbar()
{
	// put stuff before other buttons
	auto anchor = actionLoadAnnotations;
	fileLabel->setText("<i>No file selected</i>");
	toolBar->insertWidget(anchor, fileLabel);
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, partitionLabel);

	// move hierarchy slider
	auto sliderAction = toolBar->insertWidget(actionSavePlot, granularitySlider);
	// sync slider with availability & type of clustering
	sliderAction->setVisible(false);
	connect(this, &MainWindow::loadAnnotations, [sliderAction] { sliderAction->setVisible(false); });
	connect(&data, &Dataset::newHierarchy, this, [sliderAction] { sliderAction->setVisible(true); });

	// right-align screenshot & help button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	// remove container we picked from
	topBar->deleteLater();
}

void MainWindow::setupActions()
{
	/* Shortcuts (standard keys not available in UI Designer) */
	actionLoadDataset->setShortcut(QKeySequence::StandardKey::Open);
	actionSavePlot->setShortcut(QKeySequence::StandardKey::Print);
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
		emit loadDataset(filename);
	});
	connect(actionLoadAnnotations, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenClustering);
		if (filename.isEmpty())
			return;
		auto filetype = QFileInfo(filename).suffix();
		if (filetype == "json")
			emit loadHierarchy(filename);
		else
			emit loadAnnotations(filename);
	});
	connect(actionShowPartition, &QAction::toggled, chart, &Chart::togglePartitions);
	connect(actionLoadMarkers, &QAction::triggered, [this] {
		auto filename = io->chooseFile(FileIO::OpenMarkers);
		if (filename.isEmpty())
			return;
		for (auto i : data.loadMarkers(filename))
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
		data.saveMarkers(filename, indices);
	});
	connect(actionClearMarkers, &QAction::triggered, chart, &Chart::clearMarkers);
	connect(actionSavePlot, &QAction::triggered, [this] {
		io->renderToFile(chartView, title, transformSelect->currentText());
	});

	connect(actionProfileView, &QAction::triggered, [this] {
		new ProfileWindow(cursorChart, this);
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

void MainWindow::updateData()
{
	QFileInfo fi(data.peek()->source.filename);
	title = QFileInfo(fi.canonicalFilePath()).completeBaseName();
	//title = QDir(QFileInfo(fi.canonicalFilePath()).path()).dirName();
	setWindowTitle(QString("%1 – Belki").arg(title));

	chart->display(transformSelect->currentText(), true);
	fileLabel->setText(QString("<b>%1</b>").arg(title));

	/* new data means no partitions */
	actionShowPartition->setChecked(false);
	actionShowPartition->setEnabled(false);

	/* set up cursor chart */
	cursorChart->setCategories(data.peek()->dimensions);

	/* set up marker controls */
	updateMarkerControls();

	/* ready to go */
	chartView->setEnabled(true);
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
	qSort(samples.begin(), samples.end(), [&d] (const unsigned& a, const unsigned& b) {
		return d->proteins[a].name < d->proteins[b].name;
	});
	// compose list
	QString content;
	QString tpl("<b><a href='https://uniprot.org/uniprot/%1_%2'>%1</a></b> <small>%3</small><br>");
	for (auto i : qAsConst(samples)) {
		auto &p = d->proteins[i];
		auto clusters = std::accumulate(p.memberOf.begin(), p.memberOf.end(), QStringList(),
		    [&d] (QStringList a, unsigned b) { return a << d->clustering[b].name; });
		content.append(tpl.arg(p.name, p.species, clusters.join(", ")));
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
