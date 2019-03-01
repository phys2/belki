#include "mainwindow.h"
#include "dataset.h" // to register type

#include <QChartView>
#include <QApplication>
#include <QIcon>

#include <iostream>

int main(int argc, char *argv[])
{
	// register additional types needed in queued connections
	qRegisterMetaType<QVector<QColor>>();
	qRegisterMetaType<Dataset::OrderBy>();

	QApplication a(argc, argv);

	// setup our custom icon theme if there is no system theme (e.g. OSX, Windows)
	if (QIcon::themeName().isEmpty() || QIcon::themeName() == "hicolor")
		QIcon::setThemeName("Breeze");

	MainWindow window;
	window.show();

	if (argc >= 2) // pass initial filename as single argument
		window.openDataset(argv[1]);
	if (argc >= 3)
		window.importAnnotations(argv[2]);

	// proof-of-concept, works with CMake build
	// std::cout << "Running Belki " PROJECT_VERSION << std::endl;
	return a.exec();
}
