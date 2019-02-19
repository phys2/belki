#include "distmattab.h"
#include "distmatscene.h"

DistmatTab::DistmatTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
}

void DistmatTab::init(Dataset *data)
{
	scene = new DistmatScene(*data);

	connect(this, &Viewer::inUpdateColorset, scene, &DistmatScene::updateColorset);
	connect(this, &Viewer::inReset, scene, &DistmatScene::reset);
	connect(this, &Viewer::inRecolor, scene, &DistmatScene::recolor);
	connect(this, &Viewer::inReorder, scene, &DistmatScene::reorder);
	connect(this, &Viewer::inAddMarker, scene, &DistmatScene::addMarker);
	connect(this, &Viewer::inRemoveMarker, scene, &DistmatScene::removeMarker);

	connect(scene, &DistmatScene::cursorChanged, this, &Viewer::cursorChanged);

	// we are good to go on reset(true), but not on reset(false)
	connect(this, &Viewer::inReset, [this] (bool haveData) { setEnabled(haveData); });

	connect(actionToggleDistdir, &QAction::toggled, [this] (bool toggle) {
		scene->setDirection(toggle ? DistmatScene::Direction::PER_DIMENSION
		                           : DistmatScene::Direction::PER_PROTEIN);
	});

	view->setScene(scene);
}
