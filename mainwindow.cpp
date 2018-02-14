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

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), chart(new Chart), cursorChart(new QtCharts::QChart),
    fileLabel(new QLabel)
{
	setupUi(this);

	// toolbar
	fileLabel->setText("<i>No file selected</i>");
	toolBar->addWidget(fileLabel);
	toolBar->addSeparator();
	toolBar->addWidget(topBar);

	// main chart
	chartView->setChart(chart);
	chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
	chartView->setRenderHint(QPainter::Antialiasing);

	// cursor chart
	auto p = cursorInlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	cursorInlet->setPalette(p);
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	cursorChart->legend()->hide();
	cursorChart->setAxisX(new QtCharts::QValueAxis);
	cursorChart->setAxisY(new QtCharts::QValueAxis);
	cursorChart->axisY()->hide();
	cursorChart->axisX()->hide();

	/* marker controls */
	setupMarkerControls();

	/* signals */
	connect(actionLoadDataset, &QAction::triggered, [this] {
		auto filename = QFileDialog::getOpenFileName(this, "Open Dataset",
		{}, "Peak Volumnes Table (*.tsv)");
		if (filename.size())
			loadDataset(filename);
	});

	connect(chart, &Chart::cursorChanged, this, &MainWindow::updateCursorList);

	connect(transformSelect, qOverload<const QString&>(&QComboBox::currentIndexChanged),
	        [this] (const QString &name) {
		if (!data.get())
			return;
		chart->display(data->display[name]);
	});

	// TODO: qactions, buttons+shortcuts for file loading, screenshot, etc.
	// TODO: serialize markers
}

void MainWindow::loadDataset(QString filename)
{
	QFileInfo fi(filename);
	//auto title = QFileInfo(fi.canonicalFilePath()).completeBaseName();
	auto title = QDir(QFileInfo(fi.canonicalFilePath()).path()).dirName();
	setWindowTitle(QString("%1 – Belki").arg(title));

	// TODO: asynchronous computation
	fileLabel->setText(QString("<i>Calculating…</i>"));
	repaint();
	data = std::make_unique<Dataset>(filename);

	chart->setMeta(&data->proteins);
	chart->display(data->display[transformSelect->currentText()], true);
	fileLabel->setText(QString("<b>%1</b>").arg(title));

	/* set up cursor chart */
	cursorChart->axisX()->setRange(0, data->dimensions.size());

	/* set up marker controls */
	updateMarkerControls();

	/* ready to go */
	chartView->setEnabled(true);
}

void MainWindow::updateCursorList(QVector<int> samples)
{
	cursorChart->removeAllSeries();
	if (samples.empty()) {
		cursorList->clear();
		cursorWidget->setDisabled(true);
		return;
	}

	/* set up plot */
	for (auto i : samples) {
		auto s = new QtCharts::QLineSeries;
		cursorChart->addSeries(s);
		s->attachAxis(cursorChart->axisX());
		s->attachAxis(cursorChart->axisY());
		s->replace(data->featurePoints[i]);
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
	qSort(samples.begin(), samples.end(), [this] (const int& a, const int& b) {
		return data->proteins[a].firstName < data->proteins[b].firstName;
	});
	// compose list
	QString content;
	QString tpl("<a href='https://uniprot.org/uniprot/%2'>%1</a><br>");
	for (auto i : samples) {
		auto &p = data->proteins[i];
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
		for (auto m : this->markerItems)
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

}

void MainWindow::updateMarkerControls()
{
	/* re-fill model */
	auto m = qobject_cast<QStandardItemModel*>(protSearch->completer()->model());
	m->clear();
	markerItems.clear();
	for (auto i : data->protIndex)	{ // use index to have it sorted (required!)
		auto item = new QStandardItem;
		item->setText(data->proteins[i].firstName);
		item->setData(i);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		m->appendRow(item);
		markerItems[i] = item;
	}

	markerWidget->setEnabled(true);
}
