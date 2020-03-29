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
	void init(std::shared_ptr<WindowState> state);

public slots:
	void setData(std::shared_ptr<Dataset> data);
	void updateDisplay(std::vector<ProteinId> proteins, const QString &title = {});

protected:
	void updateDisplay();

	std::vector<ProteinId> proteins;

	ProfileChart *chart = nullptr;
	std::shared_ptr<Dataset> data;
	std::shared_ptr<WindowState> state;
};

#endif // PROFILEWIDGET_H
