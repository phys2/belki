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
		OpenClustering,
		OpenMarkers,
		SaveMarkers,
		SavePlot
	};
	struct RoleDef {
		QString title;
		QString filter;
		bool isWrite;
		QString writeSuffix;
	};

	explicit FileIO(QMainWindow *parent);

	QString chooseFile(Role purpose, QWidget *p = nullptr);

signals:
	void ioError(const QString &message);

public slots:
	void renderToFile(QWidget *source, const QString &title = {}, const QString &description = {});

protected:
	QMainWindow *parent; // anchor dialogs to main window
};

#endif // FILEIO_H
