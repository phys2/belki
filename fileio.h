#ifndef FILEIO_H
#define FILEIO_H

#include "utils.h"

#include <QObject>
#include <map>

class QMainWindow;

class FileIO : public QObject
{
	Q_OBJECT
public:
	enum class FileType {
		SVG, PDF, RASTERIMG
	};

	enum Role {
		OpenDataset,
		OpenDescriptions,
		OpenStructure,
		OpenMarkers,
		OpenComponents, // bnms
		OpenProject,
		SaveMarkers,
		SaveAnnotations,
		SavePlot,
		SaveProject
	};

	struct RoleDef {
		QString title;
		QString filter;
		bool isWrite;
		QString writeSuffix;
	};

	struct RenderMeta {
		QString title;
		QString description;
	};

	explicit FileIO(QMainWindow *parent);

	QString chooseFile(Role purpose, QWidget *p = nullptr);

signals:
	void ioError(const GuiMessage &message);

public slots:
	// use source::render() to create image file (source may be QWidget or QGraphicsScene)
	void renderToFile(QObject *source, const RenderMeta &meta, QString filename = {});

protected:
	QMainWindow *parent; // anchor dialogs to main window
	std::map<QString, FileType> filetypes = {
	    {"svg", FileType::SVG},
	    {"pdf", FileType::PDF},
	    {"png", FileType::RASTERIMG},
	    {"tiff", FileType::RASTERIMG},
	    {"tif", FileType::RASTERIMG},
	};
};

#endif // FILEIO_H
