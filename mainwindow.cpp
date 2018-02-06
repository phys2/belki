#include "mainwindow.h"
#include "dataset.h"

#include <QFileInfo>
#include <QStandardItemModel>
#include <QDir>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), chart(new Chart)
{
	setupUi(this);

	chartView->setChart(chart);
	chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);
	chartView->setRenderHint(QPainter::Antialiasing);

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
	//fileLabel->setText(QFileInfo(fi.canonicalFilePath()).completeBaseName());
	fileLabel->setText(QDir(QFileInfo(fi.canonicalFilePath()).path()).dirName());
	data = std::make_unique<Dataset>(filename);
	chart->setLabels(&data->labelIndex, &data->indexLabel);
	transformSelect->setCurrentIndex(1);

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
		cursorListCaption->setDisabled(true);
		return;
	}

	auto text = QString("<b>%1</b>");
	if (labels.size() > 25) {
		text.append(QString("<br>… (%1 total)").arg(labels.size()));
		labels = labels.mid(0, 20);
	}
	labels.sort();
	text = text.arg(labels.join("<br>"));
	cursorList->setText(text);
	cursorListCaption->setEnabled(true);
}
