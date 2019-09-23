#include "datahub.h"
#include "widgets/mainwindow.h"

#include <QChartView>
#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

#include <iostream>

#if defined(QT_STATIC) && defined(_WIN32)
# include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
Q_IMPORT_PLUGIN(QSvgIconPlugin)
#endif

int main(int argc, char *argv[])
{
	std::cout << "Running Belki " PROJECT_VERSION << std::endl;

	// register additional types needed in queued connections
	qRegisterMetaType<QVector<QColor>>();

	// revisit these at a later time
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

	// set aliasing for all GL views
	auto fmt = QSurfaceFormat::defaultFormat();
	fmt.setSamples(8);
	QSurfaceFormat::setDefaultFormat(fmt);

	QApplication a(argc, argv);

	// setup icons we ship as fallback for theme icons
	QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << ":/icons");
	// on non-theme platforms, we need to tell Qt to even _try_
	if (QIcon::themeName().isEmpty())
		QIcon::setThemeName("hicolor");

	/* start the application */
	DataHub hub;
	MainWindow window(hub);
	window.show();

	/* support some basic arguments */
	if (argc >= 2) // pass initial filename as single argument
		hub.importDataset(argv[1]);
	if (argc >= 3)
		hub.importAnnotations(argv[2]);

	return a.exec();
}
