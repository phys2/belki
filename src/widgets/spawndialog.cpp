#include "spawndialog.h"
#include "dataset.h"
#include "../distmat/distmatscene.h"
#include "../compute/features.h"

#include <QPushButton>
#include <QFontMetrics>

SpawnDialog::SpawnDialog(Dataset::Ptr data, std::shared_ptr<WindowState> state, QWidget *parent) :
    QDialog(parent), data(data), state(state)
{
	source_id = data->config().id;
	auto d = data->peek<Dataset::Base>();
	auto dim = (unsigned)d->dimensions.size();

	// select all by default (mirroring scene state)
	selected.resize(dim, true);

	// setup UI
	setupUi(this);
	okButton = buttonBox->button(QDialogButtonBox::StandardButton::Ok);
	setModal(true);
	setSizeGripEnabled(true);
	if (d->hasScores()) {
		scoreSpinBox->setMaximum(d->scoreRange.max);
		connect(scoreSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), [this] {
			updateScoreEffect();
			updateState();
		});
	} else {
		formLayout->removeRow(scoreLabel);
	}
	updateValidity();

	// let's blend in
	view->setBackgroundBrush(palette().window());

	// setup scene
	scene = std::make_unique<DistmatScene>(data, true);
	scene->setState(state);
	scene->setDirection(DistDirection::PER_DIMENSION);
	view->setScene(scene.get());

	// get enough space
	int heightEstimate = QFontMetrics(scene->font()).lineSpacing() * (int)dim;
	auto aspect = scene->sceneRect().width() / scene->sceneRect().height();
	view->setMinimumSize({int(heightEstimate * aspect), heightEstimate});

	connect(scene.get(), &DistmatScene::selectionChanged, [this] (const auto &s) {
		selected = s;
		updateState();
	});
	connect(this, &QDialog::accepted, this, &SpawnDialog::submit);
	connect(this, &QDialog::rejected, [this] { deleteLater(); });

	show();
}

void SpawnDialog::submit()
{
	DatasetConfiguration conf;
	conf.name = nameEdit->text();
	if (conf.name.isEmpty())
	    conf.name = nameEdit->placeholderText();
	conf.parent = source_id;

	for (unsigned i = 0; i < selected.size(); ++i)
		if (selected[i])
			conf.bands.push_back(i);

	if (scoreEffect) // otherwise, scoreSpinBox might be bust!
		conf.scoreThresh = scoreSpinBox->value();

	emit spawn(data, conf);
	deleteLater();
}

void SpawnDialog::updateState()
{
	updateValidity();

	// update default name
	bool subset = false;
	QString desc;
	for (unsigned i = 0; i < selected.size(); ++i) {
		desc.append((selected[i] ? QString::number(i + 1) : "_"));
		if (!selected[i])
			subset = true;
	}
	if (!subset)
		desc = ""; // reset
	if (scoreEffect)
		desc.append((desc.isEmpty() ? "S<" : " - S<")
					+ QString::number(scoreSpinBox->value()));
	nameEdit->setPlaceholderText(desc);
}

void SpawnDialog::updateValidity()
{
	bool valid = true;

	auto sum = std::accumulate(selected.begin(), selected.end(), unsigned(0));
	// Ensure that there is any real change in the data
	valid = valid && sum > 1 && (sum < selected.size() || scoreEffect);
	okButton->setEnabled(valid);
}

void SpawnDialog::updateScoreEffect()
{
	auto d = data->peek<Dataset::Base>();
	if (!d->hasScores())
		return; // our GUI elements were busted!

	scoreEffect = features::cutoff_effect(d->scores, scoreSpinBox->value());

	QString format{"<small>%1 / %2 proteins affected</small>"};
	scoreNote->setText(format.arg(scoreEffect).arg(d->scores.size()));
}
