#include "viewer.h"

#include <QWidget>

Viewer::Viewer(QWidget *widget, QObject *parent)
    : QObject(parent ? parent : widget), widget(widget)
{
}

void Viewer::setWindowState(std::shared_ptr<WindowState> s)
{
	if (windowState) {
		/* disconnect anything that might have been connected in override */
		windowState->disconnect(this);
		windowState->proteins().disconnect(this);
	}
	windowState = s;
	// now override might connect stuff
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
