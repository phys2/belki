#ifndef PROFILEWIDGET_H
#define PROFILEWIDGET_H

#include "ui_profilewidget.h"
#include "model.h"
#include <memory>

class ProfileChart;
class Dataset;
class WindowState;

class ProfileWidget : public QWidget, private Ui::ProfileWidget
{
	Q_OBJECT

public:
	explicit ProfileWidget(QWidget *parent = nullptr);

public slots:
	void setState(std::shared_ptr<WindowState> s) { state = s; }
	void setData(std::shared_ptr<Dataset> data);
	void updateDisplay(std::vector<ProteinId> proteins, const QString &title = {});

protected:
	void update();

	std::vector<ProteinId> proteins;

	ProfileChart *chart = nullptr;
	std::shared_ptr<Dataset> data;
	std::shared_ptr<WindowState> state;
};

#endif // PROFILEWIDGET_H
