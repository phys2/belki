#include "famscontrol.h"
#include "jobregistry.h"

#include "../compute/annotations.h"

FAMSControl::FAMSControl(QWidget *parent) :
    Viewer(new QWidget, parent)
{
	setupUi(widget);
	pruneButton->setDefaultAction(actionPruneClusters);

	stopButton->setVisible(false);
	connect(kSelect, qOverload<double>(&QDoubleSpinBox::valueChanged), [this] { configure(); });
	connect(actionPruneClusters, &QAction::toggled, [this] { configure(); });
	connect(runButton, &QToolButton::clicked, this, &FAMSControl::run);
	connect(stopButton, &QToolButton::clicked, this, &FAMSControl::stop);
}

void FAMSControl::selectDataset(unsigned id)
{
	selectData(id); // triggers updateUi()
	if (windowState->annotations.type == Annotations::Meta::MEANSHIFT)
		run();
}

void FAMSControl::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	connect(state.data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (touched & Dataset::Touch::ANNOTATIONS)
			updateUi();
	});
}

void FAMSControl::configure()
{
	/* setup desired annotations and communicate through windowState */
	Annotations::Meta desc{Annotations::Meta::MEANSHIFT};
	desc.k = kSelect->value();
	desc.pruned = actionPruneClusters->isChecked();
	if (!annotations::equal(windowState->annotations, desc)) {
		windowState->annotations = desc;
		emit windowState->annotationsChanged();
		if (windowState->orderSynchronizing && windowState->preferredOrder == Order::CLUSTERING) {
			windowState->order = {Order::CLUSTERING, windowState->annotations};
			emit windowState->orderChanged();
		}
	}
	updateUi(); // checks if already available
}

void FAMSControl::run()
{
	if (!runButton->isEnabled())
		return;

	auto desc = windowState->annotations;
	auto data = selected().data;
	// note: prepareAnnotations in our case (type MEANSHIFT) always also computes order
	Task task({[desc,data] { data->computeAnnotations(desc); },
	           Task::Type::COMPUTE_FAMS, {QString::number(desc.k, 'f', 2), data->config().name}});
	task.userData = data->config().id;
	auto monitors = windowState->jobMonitors;
	monitors.push_back(this);
	JobRegistry::run(task, monitors);
}

void FAMSControl::stop()
{
	if (!haveData())
		return;

	JobRegistry::get()->cancelJob(selected().job);
	selected().step = DataState::ABORTING;
	updateUi();
}

void FAMSControl::addJob(unsigned jobId) {
	auto state = byJob(jobId, true);
	if (!state)
		return;

	state->step = DataState::RUNNING;
	state->job = jobId;
	state->progress = 0;
	updateUi();
}

void FAMSControl::updateJob(unsigned jobId) {
	auto state = byJob(jobId);
	if (!state)
		return;

	auto job = JobRegistry::get()->job(jobId);
	if (job.isValid()) {
		state->progress = std::ceil(job.progress);
		updateUi();
	}
}

void FAMSControl::removeJob(unsigned jobId) {
	auto state = byJob(jobId);
	if (!state)
		return;

	state->step = DataState::IDLE;
	state->job = 0;
	updateUi();
}

bool FAMSControl::isAvailable()
{
	if (!haveData())
		return false;
	return selected().data->peek<Dataset::Structure>()->fetch(windowState->annotations);
}

FAMSControl::DataState *FAMSControl::byJob(unsigned jobId, bool fresh)
{
	if (fresh) { // use this only on job start where chances are high the job is still there
		auto it = states().find(JobRegistry::get()->job(jobId).userData.toUInt());
		return (it != states().end() ? dynamic_cast<DataState*>(it->second.get()) : nullptr);
	}
	for (auto &[_, state] : states()) {
		auto s = dynamic_cast<DataState*>(state.get());
		if (s && s->job == jobId)
			return s;
	}
	return nullptr;
}

void FAMSControl::updateUi()
{
	bool mayRun = haveData() && selected().step == DataState::IDLE && !isAvailable();
	bool mayStop = haveData() && selected().step == DataState::RUNNING;
	// small attempt to avoid some confusion by the user by disabling selection while computing
	// so the user does not select something else and wonders if that changes the computation or
	// later wonders where the result went. note that the user can circumvent this by selecting
	// another dataset. tough luck for the user then.
	bool maySelect = !haveData() || (selected().step != DataState::RUNNING);
	runButton->setEnabled(mayRun); // do this even if not shown; we use enabled state internally
	kSelect->setEnabled(maySelect);
	stopButton->setEnabled(mayStop);

	// update progress bar before showing it
	if (haveData()) {
		bool haveProgress = selected().step == DataState::RUNNING && selected().progress;
		progressBar->setMaximum(haveProgress ? 100 : 0); // 0 shows 'busy' animation
		progressBar->setValue(selected().progress);
	}

	// now to visibilities
	bool showProgress = haveData() && selected().step != DataState::IDLE;
	runButton->setVisible(!showProgress);
	progressBar->setVisible(showProgress);
	stopButton->setVisible(showProgress);
}
