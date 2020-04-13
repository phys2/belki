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
	JobRegistry::run(task, windowState->jobMonitors); // TODO add ourselves
}

bool FAMSControl::isAvailable()
{
	if (!haveData())
		return false;
	return selected().data->peek<Dataset::Structure>()->fetch(windowState->annotations);
}

bool FAMSControl::updateIsEnabled()
{
	/* we exploit this mechanism to update our general state, not only enabled state */
	bool mayRun = haveData() && selected().step == DataState::IDLE && !isAvailable();
	runButton->setEnabled(mayRun);
	return true;
}
