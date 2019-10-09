#ifndef PROFILEWINDOW_H
#define PROFILEWINDOW_H

#include <QMainWindow>

#include "ui_profilewindow.h"

class ProfileChart;
class MainWindow;

class ProfileWindow : public QMainWindow, private Ui::ProfileWindow
{
	Q_OBJECT

public:
	// note: we only accept MainWindows as parent
	explicit ProfileWindow(ProfileChart *source, QWidget *parent);

protected:
	ProfileChart *chart;
};

#endif // PROFILEWINDOW_H
