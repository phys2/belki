#include "mainwindow.h"
#include <QtCharts/QChartView>
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	MainWindow window;

	window.loadDataset("data/input.tsv");

	window.show();
	return a.exec();
}
