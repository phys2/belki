#include "mainwindow.h"
#include "dataset.h"

#include <QFileInfo>
#include <QStandardItemModel>
#include <QDir>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QCompleter>
#include <QAbstractProxyModel>

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
	updateMarkerControls();
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

void MainWindow::updateMarkerControls()
{
	/* create model for label list */
	auto items = data->labelIndex.keys();

	QMap<QString, QStandardItem*> ref; // back-reference for synchronization
	auto m = new QStandardItemModel(items.size(), 1);
	for (int i = 0; i < items.count(); i++)
	{
		auto item = new QStandardItem;
		item->setText(items[i]);
		item->setCheckable(true);
		item->setCheckState(Qt::Unchecked);
		m->setItem(i, item);
		ref[item->text()] = item;
	}

	/* synchronize with chart */
	connect(m, &QStandardItemModel::itemChanged, [this] (QStandardItem *i) {
		if (i->checkState() == Qt::Checked)
			chart->addMarker(i->text());
		if (i->checkState() == Qt::Unchecked)
			chart->removeMarker(i->text());
	});
	connect(chart, &Chart::markerRemoved, [ref] (const QString& idx) {
		ref[idx]->setCheckState(Qt::Unchecked);
	});

	auto cpl = new QCompleter(m);
	cpl->setCaseSensitivity(Qt::CaseInsensitive);
	cpl->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
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
