#include "spawndialog.h"
#include "distmat/distmatscene.h"
#include "mainwindow.h" // to emit signal. could also chain it.

#include <QPushButton>
#include <QFontMetrics>

SpawnDialog::SpawnDialog(Dataset &d, QWidget *parent) :
    QDialog(parent), data(d)
{
	auto dim = size_t(data.peek()->dimensions.size());

	// select all by default (mirroring scene state)
	selected.resize(dim, true);

	// setup UI
	setupUi(this);
	okButton = buttonBox->button(QDialogButtonBox::StandardButton::Ok);
	setModal(true);
	setSizeGripEnabled(true);
	updateValidity();

	// let's blend in
	view->setBackgroundBrush(palette().window());

	// setup scene
	scene = std::make_unique<DistmatScene>(data, true);
	scene->setDirection(DistmatScene::Direction::PER_DIMENSION);
	scene->reset(true);
	view->setScene(scene.get());

	// get enough space
	int heightEstimate = QFontMetrics(scene->font()).lineSpacing() * dim;
	auto aspect = scene->sceneRect().width() / scene->sceneRect().height();
	view->setMinimumSize({int(heightEstimate * aspect), heightEstimate});

	auto updater = [this] (const std::vector<bool> &s) {
		selected = s;
		QString desc;
		for (unsigned i = 0; i < selected.size(); ++i)
			desc.append((selected[i] ? QString::number(i + 1) : "_"));
		nameEdit->setPlaceholderText(desc);
		updateValidity();
	};

	connect(scene.get(), &DistmatScene::selectionChanged, updater);
	connect(this, &QDialog::accepted, this, &SpawnDialog::submit);
	connect(this, &QDialog::rejected, [this] { deleteLater(); });

	show();
}

void SpawnDialog::submit()
{
	Dataset::Configuration conf;
	// TODO: we should know ids of all datasets (including selected),
	// once the dataset selector is implemented
	conf.parent = 0;
	for (unsigned i = 0; i < selected.size(); ++i)
		if (selected[i])
			conf.bands.push_back(i);
	conf.name = nameEdit->text();
	if (conf.name.isEmpty())
	    conf.name = nameEdit->placeholderText();
	emit spawn(conf);
	deleteLater();
}

void SpawnDialog::updateValidity()
{
	bool valid = true;

	auto sum = std::accumulate(selected.begin(), selected.end(), unsigned(0));
	// TODO: right now we only allow sub-selection to spawn. but when we introduce
	// other spawning parameters, it can make sense to spawn full selection.
	valid = valid && sum > 1 && sum < selected.size();
	okButton->setEnabled(valid);
}
