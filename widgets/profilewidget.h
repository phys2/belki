#ifndef PROFILEWIDGET_H
#define PROFILEWIDGET_H

#include "ui_profilewidget.h"
#include <memory>

class ProfileChart;
class Dataset;

class ProfileWidget : public QWidget, private Ui::ProfileWidget
{
	Q_OBJECT

public:
	explicit ProfileWidget(QWidget *parent = nullptr);

public slots:
	void setData(std::shared_ptr<Dataset> data);
	void updateProteins(QVector<unsigned> samples, const QString &title = {});

protected:
	ProfileChart *chart = nullptr;
	std::shared_ptr<Dataset> data;
};

#endif // PROFILEWIDGET_H
