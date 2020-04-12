#include "bnmstab.h"
#include "bnmschart.h"
#include "referencechart.h"
#include "rangeselectitem.h"
#include "datahub.h" // for splice
#include "jobregistry.h" // for splice

// stuff for loading components. will most-probably be moved elsewhere!
#include "compute/components.h"
#include "fileio.h"
#include <QTextStream>

#include <QMainWindow>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>
#include <QAction>
#include <QToolButton>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(new QMainWindow, parent)
{
	setupUi(qobject_cast<QMainWindow*>(widget));
	referenceSelect->setModel(&proteinModel);
	auto anchor = actionHistoryMenu;
	toolBar->insertWidget(anchor, proteinBox);
	toolBar->insertSeparator(anchor);

	actionComponentToggle->setEnabled(false); // no component data yet
	actionHistoryMenu->setMenu(&historyMenu);
	actionHistoryMenu->setEnabled(false); // no history yet
	actionMarkerMenu->setMenu(&markerMenu);
	// buttons display menu without holding mouse
	for (auto i : {actionHistoryMenu, actionMarkerMenu}) {
		auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(i));
		btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);
	}

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* connect toolbar actions */
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, "Selected Profiles");
	});
	connect(actionComponentToggle, &QAction::toggled,
	        this, &BnmsTab::toggleComponentMode);
	connect(actionZoomToggle, &QAction::toggled, [this] (bool on) {
		tabState.zoomToRange = on;
		if (haveData()) selected().scene->toggleZoom(on);
	});
	connect(actionShowAverage, &QAction::toggled, [this] (bool on) {
		tabState.showAverage = on;
		if (haveData()) selected().scene->toggleAverage(on);
	});
	connect(actionShowQuantiles, &QAction::toggled, [this] (bool on) {
		tabState.showQuantiles = on;
		if (haveData()) selected().scene->toggleQuantiles(on);
	});
	connect(actionShowIndividual, &QAction::toggled, [this] (bool on) {
		if (haveData()) selected().scene->toggleIndividual(on);
	});
	connect(actionLogarithmic, &QAction::toggled, [this] (bool on) {
		if (!haveData())
			return;
		auto &current = selected();
		current.logSpace = on;
		current.scene->toggleLogSpace(on);
		current.refScene->toggleLogSpace(on);
	});
	connect(referenceSelect, qOverload<int>(&QComboBox::currentIndexChanged), [this] {
		if (referenceSelect->currentIndex() < 0)
			return; // nothing selected
		setReference((unsigned)referenceSelect->currentData(Qt::UserRole + 1).value<int>());
	});
	connect(actionLoadComponents, &QAction::triggered, this, &BnmsTab::loadComponents);
	connect(actionSplice, &QAction::triggered, [this] {
		if (!haveData())
			return;
		auto &current = selected();
		DatasetConfiguration conf;
		conf.parent = current.data->config().id;
		auto [left, right] = current.rangeSelect->range();
		conf.bands.resize(size_t(right) - size_t(left));
		std::iota(conf.bands.begin(), conf.bands.end(), unsigned(left));
		QString name("%1 to %2 (reference %3)");
		auto b = current.data->peek<Dataset::Base>();
		name = name.arg(b->dimensions.at(int(left))).arg(b->dimensions.at(int(right)));
		conf.name = name.arg(windowState->proteins().peek()->proteins[tabState.reference].name);

		Task task{[h=&windowState->hub(),source=current.data,conf] { h->spawn(source, conf); },
		          Task::Type::SPAWN, {conf.name}};
		JobRegistry::run(task, windowState->jobMonitors);
	});

	updateIsEnabled();
}

BnmsTab::~BnmsTab()
{
	deselectDataset(); // avoid double delete
}

void BnmsTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	connect(&s->proteins(), &ProteinDB::markersToggled, this, [this] {
		setupMarkerMenu();
		if (haveData()) // rebuild plot to reflect marker state change (TODO: awkward)
			selected().scene->repopulate();
	});
}

void BnmsTab::setProteinModel(QAbstractItemModel *m)
{
	//referenceSelect->setModel(m);
	proteinModel.setSourceModel(m);
}

void BnmsTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	auto &current = selected();

	// pass guiState onto chart
	auto scene = current.scene.get();
	scene->setReference(tabState.reference);
	scene->toggleComponentMode(tabState.componentMode); // TODO redundant rebuild
	scene->toggleZoom(tabState.zoomToRange);
	scene->toggleLabels(tabState.showLabels);
	scene->toggleAverage(tabState.showAverage);
	scene->toggleQuantiles(tabState.showQuantiles);
	auto refScene = current.refScene.get();
	refScene->setReference(tabState.reference);
	// TODO: refScene toggleComponentMode
	if (current.rangeSelect)
		current.rangeSelect->setSubtle(tabState.componentMode);

	// apply datastate
	actionLogarithmic->setChecked(current.logSpace);

	view->setChart(scene);
	referenceView->setChart(refScene);

	// redundant call as availability of markers is data-dependent
	setupMarkerMenu();
}

void BnmsTab::deselectDataset()
{
	// release ownerships
	view->setChart(new QtCharts::QChart());
	referenceView->setChart(new QtCharts::QChart());
	Viewer::deselectDataset();
}

void BnmsTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.components.resize(data->peek<Dataset::Base>()->features.size());
	state.scene = std::make_unique<BnmsChart>(data, state.components);
	state.refScene = std::make_unique<ReferenceChart>(data, state.components);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
		state.refScene->toggleLogSpace(true);
	}

	connect(state.refScene.get(), &ReferenceChart::componentsSelected,
	        state.scene.get(), &BnmsChart::setSelectedComponents);

	/* setup range */
	auto rightmost = data->peek<Dataset::Base>()->dimensions.size() - 1;
	state.scene->setBorder(Qt::Edge::RightEdge, rightmost);
	state.refScene->applyBorder(Qt::Edge::RightEdge, rightmost);
	if (rightmost > 10) { // does not work correctly with less than 10 dim
		auto rangeItem = std::make_unique<RangeSelectItem>(state.refScene.get());
		rangeItem->setLimits(0, rightmost);
		rangeItem->setRange(0, rightmost);
		connect(rangeItem.get(), &RangeSelectItem::borderChanged,
		        state.scene.get(), &BnmsChart::setBorder);
		connect(rangeItem.get(), &RangeSelectItem::borderChanged,
		        state.refScene.get(), &ReferenceChart::applyBorder);
		state.rangeSelect = std::move(rangeItem);
	}

	/* connect outgoing signals */
	connect(state.scene.get(), &ProfileChart::menuRequested, [this] (ProteinId id) {
		proteinMenu(id)->exec(QCursor::pos());
	});
}

std::unique_ptr<QMenu> BnmsTab::proteinMenu(ProteinId id)
{
	auto ret = windowState->proteinMenu(id);
	if (ret->actions().count() < 2)
		return ret;
	if (id == tabState.reference)
		return ret;
	auto anchor = ret->actions().at(1);
	auto action = new QAction(QIcon::fromTheme("go-next"), "Set as reference", ret.get());
	connect(action, &QAction::triggered, [this,id] { setReference(id); });
	ret->insertAction(anchor, action);
	return ret;
}

void BnmsTab::toggleComponentMode(bool on)
{
	if (tabState.componentMode == on)
		return;

	tabState.componentMode = on;
	if (!haveData())
		return;
	auto &current = selected();
	if (current.rangeSelect)
		current.rangeSelect->setSubtle(on);
	current.refScene->repopulate(); // TODO: also toggleComponentMode()
	current.scene->toggleComponentMode(on);
}

void BnmsTab::setReference(ProteinId id)
{
	if (tabState.reference == id)
		return;

	addToHistory(tabState.reference);
	tabState.reference = id;
	if (haveData()) {
		selected().scene->setReference(tabState.reference);
		selected().refScene->setReference(tabState.reference);
	}

	QSignalBlocker _(referenceSelect);
	referenceSelect->setCurrentIndex(referenceSelect->findData(id, Qt::UserRole + 1));
}

void BnmsTab::addToHistory(ProteinId id)
{
	auto name = windowState->proteins().peek()->proteins[id].name;
	auto action = new QAction(name, &historyMenu);
	connect(action, &QAction::triggered, [this,id] { setReference(id); });

	// first item ever
	if (historyMenu.isEmpty()) {
		historyMenu.addAction(action);
		actionHistoryMenu->setEnabled(true);
		return;
	}

	// remove dups from history
	const auto &entries = historyMenu.actions();
	for (auto i : entries) {
		if (i->text() == name)
			historyMenu.removeAction(i);
	}
	// add new entry
	historyMenu.insertAction(entries.first(), action);
	// limit history size
	if (entries.count() > 20)
		historyMenu.removeAction(entries.last());
}

void BnmsTab::setupMarkerMenu()
{
	actionMarkerMenu->setEnabled(haveData());
	if (!haveData())
		return;

	markerMenu.clear();
	auto p = selected().data->peek<Dataset::Proteins>();

	/* we need markers sorted */
	std::vector<ProteinId> markers(p->markers.begin(), p->markers.end());
	std::sort(markers.begin(), markers.end(), [&p] (auto a, auto b) {
		return p->proteins[a].name < p->proteins[b].name;
	});

	auto b = selected().data->peek<Dataset::Base>();
	for (auto protId : markers) {
		if (!b->protIndex.count(protId))
			continue;
		markerMenu.addAction(p->proteins[protId].name,
		                     [this,protId] { setReference(protId); });
	}

	actionMarkerMenu->setEnabled(!markerMenu.isEmpty());
}

void BnmsTab::loadComponents()
{
	if (!haveData())
		return;

	auto &io = windowState->io();
	auto fn = io.chooseFile(FileIO::OpenComponents, widget->window());
	if (fn.isEmpty())
		return;
	QFile f(fn);
	if (!f.open(QIODevice::ReadOnly)) {
		emit io.message({QString("Could not read file %1!").arg(fn)});
		return;
	}
	QTextStream in(&f);
	/* file has no header right now
	auto header = in.readLine().split("\t");
	if (header.size() != 2 || header.first() != "Protein") {
		emit io.message({"Could not parse file!",
		                 "The first column must contain protein names."});
		return;
	}
	*/

	// file parsing and storage; something that should _not_ be done here
	auto &current = selected();
	auto p = current.data->peek<Dataset::Proteins>();
	auto b = current.data->peek<Dataset::Base>();
	auto &target = current.components;
	std::fill(target.begin(), target.end(), Components{});

	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF
		if ((line.size() - 1) % 3) { // we expect triples
			emit io.message({"Could not parse complete file!",
			                 QString{"Stopped at '%1', incomplete row!"}.arg(line[0])});
			break; // avoid message flood
		}
		unsigned row;
		try {
			row = b->protIndex.at(p->find(line[0]));
		} catch (std::out_of_range&) {
			continue;
		}

		line.pop_front();
		/* hack: our profiles do not sum to 1, but as a pdf they should. so we
		 * scale the pdfs the other way round by manipulating their weights. */
		double scale = cv::sum(b->features[row])[0];
		for (auto i = 0; i < line.size(); i+=3)
			target[row].push_back({scale*line[i+2].toDouble(), // weight
			                       line[i].toDouble(), // mean
			                       line[i+1].toDouble()}); // sigma
		for (auto &i : target[row])
			i.cover = components::gauss_cover(i.mean, i.sigma,
			                                (size_t)b->dimensions.size());

		// sort by peak position
		std::sort(target[row].begin(), target[row].end(),
		          [] (const auto &a, const auto &b) { return a.mean < b.mean; });
	}

	// we have components, we want to use them
	actionComponentToggle->setEnabled(true);
	actionComponentToggle->setChecked(true);
}

bool BnmsTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	widget->setEnabled(on);
	view->setVisible(on);
	return on;
}

BnmsTab::DataState::~DataState()
{
}

QVariant BnmsTab::NoCheckstateProxyModel::data(const QModelIndex &index, int role) const
{
	// see https://bugreports.qt.io/browse/QTBUG-80197
	if (role != Qt::CheckStateRole)
		return QIdentityProxyModel::data(index, role);
	return {}; // invalid – prevent checkboxes from being drawn
}

Qt::ItemFlags BnmsTab::NoCheckstateProxyModel::flags(const QModelIndex &index) const
{
	auto flags = QIdentityProxyModel::flags(index);
	flags.setFlag(Qt::ItemFlag::ItemIsUserCheckable, false);
	return flags;
}
