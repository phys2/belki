#include "viewer.h"
#include "datahub.h"

#include <QWidget>

Viewer::Viewer(QWidget *widget, QObject *parent)
    : QObject(parent ? parent : widget), widget(widget)
{
}

void Viewer::setWindowState(std::shared_ptr<WindowState> s)
{
	if (windowState) {
		/* disconnect anything that might have been connected by us or in override */
		windowState->disconnect(this);
		windowState->proteins().disconnect(this);
	}
	windowState = s;

	// connect signals
	auto hub = &s->hub();
	connect(hub, &DataHub::newDataset, this, &Viewer::addDataset);
	connect(hub, &DataHub::datasetRemoved, this, &Viewer::removeDataset);

	// get up to speed
	for (auto &[_, d] : hub->datasets())
		addDataset(d);

	// now override might connect more stuff
}

void Viewer::deselectDataset()
{
	dataIt = dataStates.end();
	updateIsEnabled();
}

void Viewer::removeDataset(unsigned id)
{
	if (dataStates.find(id) == dataIt)
		deselectDataset();
	auto currentId = (dataIt == dataStates.end() ? 0 : dataIt->first);
	dataStates.erase(id);
	dataIt = dataStates.find(currentId); // update iterator after map changed
}

bool Viewer::haveData() {
	return dataIt != dataStates.end();
}

bool Viewer::selectData(unsigned id)
{
	dataIt = dataStates.find(id);
	return updateIsEnabled();
}

Viewer::ContentMap::iterator Viewer::addData(unsigned id, Viewer::DataState::Ptr elem)
{
	auto currentId = (dataIt == dataStates.end() ? 0 : dataIt->first);
	auto [newIt, _] = dataStates.insert(std::make_pair(id, std::move(elem)));
	dataIt = dataStates.find(currentId); // update iterator after map changed
	return newIt;
}

bool Viewer::updateIsEnabled()
{
	return haveData();
}
