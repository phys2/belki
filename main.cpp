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

/* all instances, for proper cleanup */
static std::unordered_map<DataHub*,GuiState*> instances;

void setupQt()
{
	// register additional types needed in queued connections
	qRegisterMetaType<QVector<QColor>>();
	qRegisterMetaType<MessageType>();
	qRegisterMetaType<Protein>("Protein"); // needed for signal
	qRegisterMetaType<ProteinId>("ProteinId"); // needed for typedefs
	qRegisterMetaType<std::vector<ProteinId>>("std::vector<ProteinId>"); // needed for signal

	// set HIDPI attributes before creating QApplication
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

	// set aliasing for all GL views
	auto fmt = QSurfaceFormat::defaultFormat();
	fmt.setSamples(4);
	QSurfaceFormat::setDefaultFormat(fmt);

	// setup icons we ship as fallback for theme icons
	QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << ":/icons");
	// on non-theme platforms, we need to tell Qt to even _try_
	if (QIcon::themeName().isEmpty())
		QIcon::setThemeName("hicolor");

	// set application metadata
	QApplication::setApplicationName("Belki");
	QApplication::setApplicationVersion(PROJECT_VERSION);
}

void instantiate(QString filename)
{
	/* create instance elements */
	auto hub = new DataHub;
	auto gui = new GuiState(*hub);
	instances[hub] = gui;

	/* hook cleanup */
	gui->connect(gui, &GuiState::closed, [hub] {
		delete instances[hub]; // gui
		instances.erase(hub);
		delete hub;

		if (instances.empty())
			QApplication::quit();
	});

	/* hook forking */
	gui->connect(gui, &GuiState::instanceRequested, [] (const QString& fn) {
		instantiate(fn);
	});

	/* fire up */
	gui->addWindow(); // open window first for wired error messages
	if (!filename.isEmpty())
		hub->openProject(filename);
}

void cleanup()
{
	for (auto &[k, v] : instances) {
		delete v;
		delete k;
	}
	instances.clear();
}

int main(int argc, char *argv[])
{
	std::cout << "Running Belki " PROJECT_VERSION;
	std::cout << " built " << PROJECT_DATE << std::endl;

	setupQt();

	/* start the application */
	QApplication a(argc, argv);
	a.setQuitOnLastWindowClosed(false);

	/* start initial instance */
	instantiate(argc >= 2 ? argv[1] : QString{});

	/* cleanup */
	a.connect(&a, &QApplication::aboutToQuit, [] { cleanup(); });

	return a.exec();
}
