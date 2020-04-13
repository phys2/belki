#include "famscontrol.h"
#include "jobregistry.h"

FAMSControl::FAMSControl(QWidget *parent) :
    Viewer(new QWidget, parent)
{
	setupUi(widget);

	stopButton->setVisible(false);
	connect(kSelect, qOverload<double>(&QDoubleSpinBox::valueChanged), [this] { configure(); });
	connect(runButton, &QToolButton::clicked, this, &FAMSControl::run);
}

FAMSControl::~FAMSControl()
{
	// cancel all jobs one last time?
}

void FAMSControl::selectDataset(unsigned id)
{
	selectData(id); // triggers updateIsEnabled()
	if (windowState->annotations.type == Annotations::Meta::MEANSHIFT)
		run();
}

void FAMSControl::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	connect(state.data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (touched & Dataset::Touch::CLUSTERS)
			updateIsEnabled();
	});
}

void FAMSControl::configure()
{
	/* setup desired annotations and communicate through windowState */
	Annotations::Meta desc{Annotations::Meta::MEANSHIFT};
	desc.k = kSelect->value();
	if (windowState->annotations.type != Annotations::Meta::MEANSHIFT
	    || windowState->annotations.k != desc.k) {
		windowState->annotations = desc;
		emit windowState->annotationsChanged();
		if (windowState->orderSynchronizing && windowState->preferredOrder == Order::CLUSTERING) {
			windowState->order = {Order::CLUSTERING, windowState->annotations};
			emit windowState->orderChanged();
		}
	}

	updateIsEnabled(); // checks if already available
}

void FAMSControl::run()
{
	if (!runButton->isEnabled())
		return;

	auto desc = windowState->annotations;
	auto data = selected().data;
	// note: prepareAnnotations in our case (type MEANSHIFT) always also computes order
	Task task({[desc,data] { data->prepareAnnotations(desc); },
	           Task::Type::COMPUTE_FAMS, {QString::number(desc.k, 'f', 2), data->config().name}});
	task.userData = data->config().id;
	auto monitors = windowState->jobMonitors;
	monitors.push_back(this);
	JobRegistry::run(task, monitors);
}

void FAMSControl::addJob(unsigned jobId) {
	auto state = byJob(jobId, true);
	if (!state)
		return;

	state->step = DataState::RUNNING;
	state->job = jobId;
	updateIsEnabled();
}

void FAMSControl::updateJob(unsigned) {
	// TODO progress updates with a progress bar
}

void FAMSControl::removeJob(unsigned jobId) {
	auto state = byJob(jobId);
	if (!state)
		return;

	state->step = DataState::IDLE;
	state->job = 0;
	updateIsEnabled();
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

bool FAMSControl::updateIsEnabled()
{
	/* we exploit this mechanism to update our general state, not only enabled state */
	bool mayRun = haveData() && selected().step == DataState::IDLE && !isAvailable();
	// small attempt to avoid some confusion by the user by disabling selection while computing
	// so the user does not select something else and wonders if that changes the computation or
	// later wonders where the result went. note that the user can circumvent this by selecting
	// another dataset. tough luck for the user then.
	bool maySelect = !haveData() || (selected().step != DataState::RUNNING);
	runButton->setEnabled(mayRun);
	kSelect->setEnabled(maySelect);
	return true;
}
