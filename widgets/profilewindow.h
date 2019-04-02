#ifndef PROFILEWINDOW_H
#define PROFILEWINDOW_H

#include <QMainWindow>

#include "ui_profilewindow.h"

class MainWindow;
class ProfileChart;

class ProfileWindow : public QMainWindow, private Ui::ProfileWindow
{
	Q_OBJECT

	// hack override to cast to right type.
	// Note: only works with object refence (e.g. internally) because non-virtual
	MainWindow *parentWidget();

public:
	explicit ProfileWindow(ProfileChart *source, MainWindow *parent = nullptr);

protected:
	ProfileChart *chart;
};

#endif // PROFILEWINDOW_H
