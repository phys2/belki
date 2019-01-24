#include "mainwindow.h"
#include "dataset.h" // to register type
#include <QChartView>
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
	qRegisterMetaType<Dataset::OrderBy>(); // needed for queued connections

	QApplication a(argc, argv);

	// setup our custom icon theme if there is no system theme (e.g. OSX, Windows)
	if (QIcon::themeName().isEmpty() || !QIcon::themeName().compare("hicolor"))
		QIcon::setThemeName("Breeze");

	MainWindow window;
	window.show();

	if (argc >= 2) // pass initial filename as single argument
		window.openDataset(argv[1]);
	if (argc >= 3)
		window.importAnnotations(argv[2]);

	return a.exec();
}
