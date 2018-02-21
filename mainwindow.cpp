#include "mainwindow.h"
#include "dataset.h"
#include "chart.h"

#include <QFileInfo>
#include <QStandardItemModel>
#include <QDir>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QCompleter>
#include <QAbstractProxyModel>
#include <QtWidgets/QLabel>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), chart(new Chart(data)), cursorChart(new QtCharts::QChart),
    fileLabel(new QLabel)
{
	data.moveToThread(&dataThread);
	dataThread.start();

	setupUi(this);

	/* actions */
	// standard keys not available in UI Designer
	actionLoadDataset->setShortcut(QKeySequence::StandardKey::Open);
	actionHelp->setShortcut(QKeySequence::StandardKey::HelpContents);

	/* toolbar */
	// put stuff before help button
	auto anchor = actionHelp;
	fileLabel->setText("<i>No file selected</i>");
	toolBar->insertWidget(anchor, fileLabel);
	toolBar->insertSeparator(anchor);
	toolBar->insertWidget(anchor, topBar);
	// right-align help button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionHelp, spacer);

	/* main chart */
	chartView->setChart(chart);
	chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* cursor chart */
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	cursorChart->legend()->hide();
	cursorChart->setAxisX(new QtCharts::QValueAxis);
	cursorChart->setAxisY(new QtCharts::QValueAxis);
	cursorChart->axisY()->hide();
	cursorChart->axisX()->hide();
	// common background for plot and its container
	auto p = cursorInlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	cursorInlet->setPalette(p);

	/* marker controls */
	setupMarkerControls();

	/* signals */
	connect(this, &MainWindow::loadDataset, &data, &Dataset::loadDataset);
	connect(this, &MainWindow::loadAnnotations, &data, &Dataset::loadAnnotations);
	connect(&data, &Dataset::ioError, this, &MainWindow::displayError);
	connect(&data, &Dataset::newData, this, &MainWindow::updateData);

	connect(actionHelp, &QAction::triggered, this, &MainWindow::showHelp);
	connect(actionLoadDataset, &QAction::triggered, [this] {
		auto filename = QFileDialog::getOpenFileName(this, "Open Dataset",
		{}, "Peak Volumnes Table (*.tsv)");
		if (filename.isEmpty())
			return;
		fileLabel->setText(QString("<i>Calculating…</i>"));
		emit loadDataset(filename);
	});

	connect(chart, &Chart::cursorChanged, this, &MainWindow::updateCursorList);

	connect(transformSelect, qOverload<const QString&>(&QComboBox::currentIndexChanged),
	        [this] (const QString &name) { chart->display(name); });

	// TODO: qactions, buttons+shortcuts for screenshot, etc.
}

MainWindow::~MainWindow()
{
	dataThread.quit();
	dataThread.wait();
}

void MainWindow::updateData(const QString &filename)
{
	QFileInfo fi(filename);
	//auto title = QFileInfo(fi.canonicalFilePath()).completeBaseName();
	auto title = QDir(QFileInfo(fi.canonicalFilePath()).path()).dirName();
	setWindowTitle(QString("%1 – Belki").arg(title));

	chart->display(transformSelect->currentText(), true);
	fileLabel->setText(QString("<b>%1</b>").arg(title));

	/* set up cursor chart */
	cursorChart->axisX()->setRange(0, data.peek()->dimensions.size());

	/* set up marker controls */
	updateMarkerControls();

	/* ready to go */
	chartView->setEnabled(true);
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

void MainWindow::updateCursorList(QVector<int> samples)
{
	auto d = data.peek();
	cursorChart->removeAllSeries();
	if (samples.empty()) {
		cursorList->clear();
		// only change title to avoid geometry change under Windows
		cursorWidgetCaption->setDisabled(true);
		return;
	}

	/* set up plot */
	for (auto i : qAsConst(samples)) {
		auto s = new QtCharts::QLineSeries;
		cursorChart->addSeries(s);
		s->attachAxis(cursorChart->axisX());
		s->attachAxis(cursorChart->axisY());
		s->replace(d->featurePoints[i]);
	}

	/* set up list */

	// reduce set
	const int showMax = 25;
	auto text = QString("<b>%1</b>");
	if (samples.size() > showMax) {
		text.append(QString("… (%1 total)").arg(samples.size()));
		samples.resize(showMax - 1);
	}
	// sort by name
	qSort(samples.begin(), samples.end(), [&d] (const int& a, const int& b) {
		return d->proteins[a].firstName < d->proteins[b].firstName;
	});
	// compose list
	QString content;
	QString tpl("<a href='https://uniprot.org/uniprot/%2'>%1</a><br>");
	for (auto i : qAsConst(samples)) {
		auto &p = d->proteins[i];
		content.append(tpl.arg(p.firstName, p.name));
	}
	cursorList->setText(text.arg(content));

	cursorWidgetCaption->setEnabled(true);
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
	connect(chart, &Chart::markerToggled, [this] (int idx, bool present) {
		this->markerItems[idx]->setCheckState(present ? Qt::Checked : Qt::Unchecked);
	});
	connect(chart, &Chart::markersCleared, [this] () {
		for (auto m : qAsConst(this->markerItems))
			m->setCheckState(Qt::Unchecked);
	});
	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		if (i->checkState() == Qt::Checked)
			chart->addMarker(i->data().toInt());
		if (i->checkState() == Qt::Unchecked)
			chart->removeMarker(i->data().toInt());
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

	/* setup buttons / actions, TODO move to more appropriate place */
	loadMarkersButton->setDefaultAction(actionLoadMarkers);
	saveMarkersButton->setDefaultAction(actionSaveMarkers);
	clearMarkersButton->setDefaultAction(actionClearMarkers);
	connect(actionLoadMarkers, &QAction::triggered, [this] {
		auto filename = QFileDialog::getOpenFileName(this, "Open Markers File",
		{}, "List of markers (*.txt);; All Files (*)");
		if (filename.isEmpty())
			return;
		if (QFileInfo(filename).suffix().isEmpty())
			filename.append(".txt");
		for (auto i : data.loadMarkers(filename))
			chart->addMarker(i);
	});
	connect(actionSaveMarkers, &QAction::triggered, [this] {
		auto filename = QFileDialog::getSaveFileName(this, "Save Markers to File",
		{}, "List of markers (*.txt)");
		if (filename.isEmpty())
			return;
		QVector<int> indices;
		for (auto m : qAsConst(this->markerItems))
			if (m->checkState() == Qt::Checked)
				indices.append(m->data().toInt());
		data.saveMarkers(filename, indices);
	});
	connect(actionClearMarkers, &QAction::triggered, chart, &Chart::clearMarkers);
}

void MainWindow::updateMarkerControls()
{
	/* re-fill model */
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->clear();
	markerItems.clear();
	auto d = data.peek();
	for (auto i : qAsConst(d->protIndex)) { // use index to have it sorted (required!)
		auto item = new QStandardItem;
		item->setText(d->proteins[i].firstName);
		item->setData(i);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		m->appendRow(item);
		markerItems[i] = item;
	}

	markerWidget->setEnabled(true);
}
