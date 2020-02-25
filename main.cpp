#include "datahub.h"
#include "guistate.h"
#include "utils.h"

// for registering meta types
#include "model.h"

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
	std::cout << "Running Belki " PROJECT_VERSION;
	std::cout << " built " << PROJECT_DATE << std::endl;

	// register additional types needed in queued connections
	qRegisterMetaType<QVector<QColor>>();
	qRegisterMetaType<MessageType>();
	qRegisterMetaType<Protein>("Protein"); // needed for signal
	qRegisterMetaType<ProteinId>("ProteinId"); // needed for typedefs
	qRegisterMetaType<std::vector<ProteinId>>("std::vector<ProteinId>"); // needed for signal

	// revisit these at a later time
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

	// set aliasing for all GL views
	auto fmt = QSurfaceFormat::defaultFormat();
	fmt.setSamples(4);
	QSurfaceFormat::setDefaultFormat(fmt);

	QApplication a(argc, argv);
	a.setQuitOnLastWindowClosed(false);

	// setup icons we ship as fallback for theme icons
	QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << ":/icons");
	// on non-theme platforms, we need to tell Qt to even _try_
	if (QIcon::themeName().isEmpty())
		QIcon::setThemeName("hicolor");

	/* start the application */
	DataHub hub;
	GuiState gui(hub);
	gui.addWindow();

	/* support some basic arguments */
	if (argc >= 2) { // pass project filename as single argument
		auto datasets = hub.store.openProject(argv[1]);
		hub.init(datasets);
	}

	return a.exec();
}
