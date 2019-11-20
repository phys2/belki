#ifndef SPAWNDIALOG_H
#define SPAWNDIALOG_H

#include "ui_spawndialog.h"

#include <memory>

class Dataset;
class DatasetConfiguration;
class WindowState;
class DistmatScene;
class QPushButton;

class SpawnDialog : public QDialog, private Ui::SpawnDialog
{
	Q_OBJECT

public:
	SpawnDialog(std::shared_ptr<Dataset> data, std::shared_ptr<WindowState> state, QWidget *parent = nullptr);

signals:
	void spawn(std::shared_ptr<Dataset const> source, const DatasetConfiguration& config);

public slots:
	void updateState();

protected:
	void updateValidity();
	void submit();

	void updateScoreEffect();

	unsigned source_id;
	std::vector<bool> selected;
	unsigned scoreEffect = 0; // number of proteins affected by score cutoff

	std::unique_ptr<DistmatScene> scene;
	QPushButton *okButton; // cached, owned by Ui::SpawnDialog

	// data source & window state
	std::shared_ptr<Dataset> data;
	std::shared_ptr<WindowState> state;
};

#endif // SPAWNDIALOG_H
