#include "mainwindow.h"
#include <QtCharts/QChartView>
#include <QtWidgets/QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);

	// setup our custom icon theme if there is no system theme (e.g. OSX, Windows)
	if (QIcon::themeName().isEmpty() || !QIcon::themeName().compare("hicolor"))
	    QIcon::setThemeName("breeze");

	MainWindow window;

	window.loadDataset("data/input.tsv");

	window.show();
	return a.exec();
}
