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
	cursorPlot->setChart(cursorChart);
	cursorPlot->setRenderHint(QPainter::Antialiasing);
	cursorChart->legend()->hide();
	cursorChart->setAxisX(new QtCharts::QValueAxis);
	cursorChart->setAxisY(new QtCharts::QValueAxis);
	cursorChart->axisY()->hide();
	cursorChart->axisX()->hide();

	// signals
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
	fileLabel->setText(QString("<i>Calculating…</i>").arg(title));
	repaint();
	data = std::make_unique<Dataset>(filename);

	chart->setMeta(&data->proteins, &data->protIndex);
	chart->display(data->display[transformSelect->currentText()], true);
	fileLabel->setText(QString("<b>%1</b>").arg(title));

	/* set up cursor chart */
	cursorChart->axisX()->setRange(0, data->dimensions.size());

	/* set up marker controls */
	updateMarkerControls();

	/* ready to go */
	chartView->setEnabled(true);
}

void MainWindow::updateCursorList(QStringList labels)
{
	if (labels.empty()) {
		cursorList->clear();
		cursorChart->removeAllSeries();
		cursorWidget->setDisabled(true);
		return;
	}

	/* set up plot */
	cursorChart->removeAllSeries();
	for (int i = 0; i < labels.size(); ++i) {
		auto s = new QtCharts::QLineSeries;
		cursorChart->addSeries(s);
		s->attachAxis(cursorChart->axisX());
		s->attachAxis(cursorChart->axisY());
		s->replace(data->featurePoints[data->protIndex[labels[i]]]);
	}

	/* set up list */
	auto text = QString("<b>%1</b>");
	if (labels.size() > 25) {
		text.append(QString("<br>… (%1 total)").arg(labels.size()));
		labels = labels.mid(0, 20);
	}
	labels.sort();
	text = text.arg(labels.join("<br>"));
	cursorList->setText(text);
	cursorWidget->setEnabled(true);
}

void MainWindow::updateMarkerControls()
{
	/* create model for label list */
	QMap<QString, QStandardItem*> ref; // back-reference for synchronization
	auto m = new QStandardItemModel;
	for (auto i : data->protIndex)	{ // use index to have it sorted
		auto item = new QStandardItem;
		item->setText(data->proteins[i].name);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		m->appendRow(item);
		ref[item->text()] = item;
	}

	/* synchronize with chart */
	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		if (i->checkState() == Qt::Checked)
			chart->addMarker(i->text());
		if (i->checkState() == Qt::Unchecked)
			chart->removeMarker(i->text());
	});
	connect(chart, &Chart::markerToggled, [ref] (const QString& idx, bool present) {
		ref[idx]->setCheckState(present ? Qt::Checked : Qt::Unchecked);
	});

	/* setup completer */
	auto cpl = new QCompleter(m);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel); // see above
	cpl->setCompletionMode(QCompleter::InlineCompletion);
	protSearch->setCompleter(cpl);
	protList->setModel(cpl->completionModel());

	/* Allow to toggle check state by click */
	connect(protList, &QListView::clicked, [m] (QModelIndex i) {
		if (!i.isValid())
			return; // didn't click on a row, e.g. clicked on a checkmark
		auto proxy = qobject_cast<const QAbstractProxyModel*>(i.model());
		if (!proxy)
			return; // sorry, can't do this!
		auto item = m->itemFromIndex(proxy->mapToSource(i));
		item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
	});

	/* Allow to add protein by pressing enter in protSearch */
	connect(protSearch, &QLineEdit::returnPressed, [this, cpl] {
		auto target = cpl->currentCompletion();
		if (target.size())
			chart->addMarker(target);
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

	markerWidget->setEnabled(true);
}
