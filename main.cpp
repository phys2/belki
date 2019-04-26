#include "mainwindow.h"
#include "dataset.h" // to register type

#include <QChartView>
#include <QApplication>
#include <QIcon>

#include <iostream>

#if defined(QT_STATIC) && defined(_WIN32)
# include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
Q_IMPORT_PLUGIN(QSvgIconPlugin)
#endif

int main(int argc, char *argv[])
{
	// register additional types needed in queued connections
	qRegisterMetaType<QVector<QColor>>();
	qRegisterMetaType<Dataset::OrderBy>();
	qRegisterMetaType<Dataset::Configuration>();

	// revisit these at a later time
	//QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	//QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

	QApplication a(argc, argv);

	// setup icons we ship as fallback for theme icons
	QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << ":/icons");
	// on non-theme platforms, we need to tell Qt to even _try_
	if (QIcon::themeName().isEmpty())
		QIcon::setThemeName("hicolor");

	MainWindow window;
	window.show();

	if (argc >= 2) // pass initial filename as single argument
		window.openDataset(argv[1]);
	if (argc >= 3)
		window.importAnnotations(argv[2]);

	std::cout << "Running Belki " PROJECT_VERSION << std::endl;
	return a.exec();
}
