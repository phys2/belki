#include "mainwindow.h"
#include "dataset.h"

#include <QFileInfo>
#include <QStandardItemModel>
#include <QDir>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QtDebug>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), chart(new Chart), cursorChart(new QtCharts::QChart)
{
	setupUi(this);

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
	connect(chart, &Chart::cursorChanged, this, &MainWindow::updateCursorList);
	connect(protSearch, qOverload<const QString&>(&QComboBox::currentIndexChanged),
	        chart, &Chart::addMarker);

	connect(transformSelect, qOverload<const QString&>(&QComboBox::currentIndexChanged),
	        [this] (const QString &name) {
		if (!data.get())
			return;
		chart->display("All proteins", data->display[name]);
	});

	// TODO: buttons for file loading, screenshot.
	// TODO: serialize markers
}

void MainWindow::loadDataset(QString filename)
{
	QFileInfo fi(filename);
	//auto title = QFileInfo(fi.canonicalFilePath()).completeBaseName();
	auto title = QDir(QFileInfo(fi.canonicalFilePath()).path()).dirName();
	setWindowTitle(QString("%1 – Belki").arg(title));
	fileLabel->setText(title);

	data = std::make_unique<Dataset>(filename);
	chart->setLabels(&data->labelIndex, &data->indexLabel);
	transformSelect->setCurrentIndex(1);

	/* set up cursor chart */
	cursorChart->axisX()->setRange(0, data->dimensions.size());

	/* set up marker controls */
	protSearch->blockSignals(true);
	// TODO: use QCompleter. Probably use a QTextEdit+QCompleter / QListWidget combo
	auto items = data->labelIndex.keys();
	/*auto m = new QStandardItemModel(items.size(), 1);
	for (int i = 0; i < items.count(); i++)
	{
	    auto item = new QStandardItem;
	    item->setText(items[i]);
	    item->setCheckable(true);
	    item->setCheckState(Qt::Unchecked);
	    m->setItem(i, item);
	}*/
	//protSearch->setModel(m);
	protSearch->addItems(items);
	protSearch->blockSignals(false);
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
		s->replace(data->featurePoints[data->labelIndex[labels[i]]]);
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
