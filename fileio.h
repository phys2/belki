#ifndef FILEIO_H
#define FILEIO_H

#include <QObject>

class QMainWindow;

class FileIO : public QObject
{
	Q_OBJECT
public:
	enum Role {
		OpenDataset,
		OpenDescriptions,
		OpenClustering,
		OpenMarkers,
		SaveMarkers,
		SaveAnnotations,
		SavePlot
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
	void ioError(const QString &message);

public slots:
	void renderToFile(QWidget *source, const RenderMeta &meta, QString filename = {});

protected:
	QMainWindow *parent; // anchor dialogs to main window
};

#endif // FILEIO_H
