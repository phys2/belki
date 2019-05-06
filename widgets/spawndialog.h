#ifndef SPAWNDIALOG_H
#define SPAWNDIALOG_H

#include "ui_spawndialog.h"
#include "dataset.h"

#include <memory>

class DistmatScene;
class QPushButton;

class SpawnDialog : public QDialog, private Ui::SpawnDialog
{
	Q_OBJECT

public:
	explicit SpawnDialog(Dataset &data, QWidget *parent = nullptr);

signals:
	void spawn(const Dataset::Configuration& config);

protected:
	void updateValidity();
	void submit();

	unsigned source_id;
	std::vector<bool> selected;

	std::unique_ptr<DistmatScene> scene;
	QPushButton *okButton; // cached, owned by Ui::SpawnDialog

	// data source
	Dataset &data;
};

#endif // SPAWNDIALOG_H
