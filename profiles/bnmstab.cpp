#include "bnmstab.h"
#include "bnmschart.h"
#include "referencechart.h"
#include "rangeselectitem.h"
#include "compute/features.h"

// stuff for loading components. will most-probably be moved elsewhere!
#include "widgets/mainwindow.h"
#include "fileio.h"
#include <QTextStream>

#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QListWidget>
#include <QAction>
#include <QToolButton>

BnmsTab::BnmsTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);
	auto anchor = actionHistoryMenu;
	toolBar->insertWidget(anchor, proteinBox);
	toolBar->insertSeparator(anchor);

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
	connect(actionShowAverage, &QAction::toggled, [this] (bool on) {
		tabState.showAverage = on;
		if (current) current().scene->toggleAverage(on);
	});
	connect(actionShowQuantiles, &QAction::toggled, [this] (bool on) {
		tabState.showQuantiles = on;
		if (current) current().scene->toggleQuantiles(on);
	});
	connect(actionShowIndividual, &QAction::toggled, [this] (bool on) {
		if (current) current().scene->toggleIndividual(on);
	});
	connect(actionLogarithmic, &QAction::toggled, [this] (bool on) {
		if (current) {
			current().logSpace = on;
			current().scene->toggleLogSpace(on);
			current().refScene->toggleLogSpace(on);
		}
	});
	connect(referenceSelect, qOverload<int>(&QComboBox::currentIndexChanged), [this] {
		if (referenceSelect->currentIndex() < 0)
			return; // nothing selected
		setReference((unsigned)referenceSelect->currentData(Qt::UserRole + 1).value<int>());
	});
	connect(actionLoadComponents, &QAction::triggered, this, &BnmsTab::loadComponents);

	updateEnabled();
}

void BnmsTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	connect(&s->proteins(), &ProteinDB::markersToggled, [this] {
		setupMarkerMenu();
		if (current) // rebuild plot to reflect marker state change (TODO: awkward)
			current().scene->repopulate();
	});
}

void BnmsTab::setProteinModel(QAbstractItemModel *m)
{
	referenceSelect->setModel(m);
}

void BnmsTab::selectDataset(unsigned id)
{
	current = {id, &content[id]};
	updateEnabled();

	if (!current)
		return;

	// pass guiState onto chart
	auto scene = current().scene.get();
	scene->setReference(tabState.reference);
	scene->toggleLabels(tabState.showLabels);
	scene->toggleAverage(tabState.showAverage);
	scene->toggleQuantiles(tabState.showQuantiles);
	auto refScene = current().refScene.get();
	refScene->setReference(tabState.reference);
	current().rangeSelect->setSubtle(tabState.componentMode);

	// apply datastate
	actionLogarithmic->setChecked(current().logSpace);

	view->setChart(scene);
	referenceView->setChart(refScene);

	// redundant call as availability of markers is data-dependent
	setupMarkerMenu();
}

void BnmsTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<BnmsChart>(data);
	state.components.resize(data->peek<Dataset::Base>()->features.size());
	state.refScene = std::make_unique<ReferenceChart>(data, state.components);
	if (data->peek<Dataset::Base>()->logSpace) {
		state.logSpace = true;
		state.scene->toggleLogSpace(true);
		state.refScene->toggleLogSpace(true);
	}

	/* setup range */
	auto ndim = data->peek<Dataset::Base>()->dimensions.size();
	state.scene->setBorder(Qt::Edge::RightEdge, ndim);
	state.refScene->applyBorder(Qt::Edge::RightEdge, ndim);
	if (ndim > 10) { // does not work correctly with less than 10 dim
		auto rangeItem = std::make_unique<RangeSelectItem>(state.refScene.get());
		rangeItem->setLimits(0, ndim);
		rangeItem->setRange(0, ndim);
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

void BnmsTab::setReference(ProteinId id)
{
	if (tabState.reference == id)
		return;

	addToHistory(tabState.reference);
	tabState.reference = id;
	if (current) {
		current().scene->setReference(tabState.reference);
		current().refScene->setReference(tabState.reference);
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
	actionMarkerMenu->setEnabled(current);
	if (!current)
		return;

	markerMenu.clear();
	auto p = current().data->peek<Dataset::Proteins>();

	/* we need markers sorted */
	std::vector<ProteinId> markers(p->markers.begin(), p->markers.end());
	std::sort(markers.begin(), markers.end(), [&p] (auto a, auto b) {
		return p->proteins[a].name < p->proteins[b].name;
	});

	auto b = current().data->peek<Dataset::Base>();
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
	if (!current)
		return;

	auto io = qobject_cast<MainWindow*>(window())->getIo();
	auto fn = io->chooseFile(FileIO::OpenComponents);
	if (fn.isEmpty())
		return;
	QFile f(fn);
	if (!f.open(QIODevice::ReadOnly)) {
		emit io->ioError(QString("Could not read file %1!").arg(fn));
		return;
	}
	QTextStream in(&f);
	/* file has no header right now
	auto header = in.readLine().split("\t");
	if (header.size() != 2 || header.first() != "Protein") {
		emit io->ioError(QString("Could not parse file!<p>The first column must contain protein names.</p>").arg(fn));
		return;
	}
	*/

	// file parsing and storage; something that should _not_ be done here
	auto p = current().data->peek<Dataset::Proteins>();
	auto b = current().data->peek<Dataset::Base>();
	auto &target = current().components;
	std::fill(target.begin(), target.end(), Components{});

	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF
		if ((line.size() - 1) % 3) { // we expect triples
			emit io->ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
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
	}
	// TODO: have an action and toggle it
	tabState.componentMode = true;
	current().rangeSelect->setSubtle(true); // TODO: remove, action toggle takes over
	current().refScene->repopulate(); // TODO: remove, action toggle takes over
}

void BnmsTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}

BnmsTab::DataState::~DataState()
{
}
