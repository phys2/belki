#include "jobstatus.h"
#include "jobregistry.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolTip>
#include <QHBoxLayout>
#include <QStyle>
#include <QStyleOptionButton>

JobStatus::JobStatus(QWidget *parent)
    : QWidget(parent)
{
	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// provide shared renderer and animation timer for all children
	renderer = new QSvgRenderer(QString{":/spinner.svg"}, this);
	renderer->setFramesPerSecond(fps);
	// extra timer as QSvgRenderer::repaintNeeded() signal seems not to respect FPS
	animator = new QTimer(this);
	animator->setInterval(1000 / fps);
	animator->setSingleShot(false);
	connect(animator, &QTimer::timeout, [this] { update(); });
}

void JobStatus::addJob(unsigned id)
{
	auto widget = new JobWidget(id, renderer);
	layout()->addWidget(widget); // sets parent
	jobs.try_emplace(id, widget);
	updateJobs();
}

void JobStatus::updateJob(unsigned id)
{
	try {
		jobs.at(id)->update();
	}  catch (std::out_of_range&) {} // TODO complain, state error on caller side
}

void JobStatus::removeJob(unsigned id)
{
	try {
		delete jobs.at(id);
	}  catch (std::out_of_range&) {} // TODO complain, state error on caller side
	jobs.erase(id);
	updateJobs();
}

void JobStatus::updateJobs()
{
	if (jobs.empty())
		animator->stop();
	else
		animator->start();
}

JobWidget::JobWidget(unsigned jobId, QSvgRenderer *renderer, QWidget *parent)
    : QWidget(parent), renderer(renderer), jobId(jobId)
{
	// allow expansion in y-direction, and honor the width we set on resizes
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	setMouseTracking(true);
}

void JobWidget::resizeEvent(QResizeEvent *event)
{
	auto newHeight = event->size().height();
	// ensure square size
	if (newHeight != event->oldSize().height())
		setMinimumWidth(newHeight);
}

void JobWidget::mouseMoveEvent(QMouseEvent *event)
{
	auto text = JobRegistry::get()->job(jobId).name;
	QToolTip::showText(event->globalPos(), text, this);
}

void JobWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	/* draw a tool-button style panel so we look like a button; includes hover effects */
	QStyleOptionButton opt;
	opt.initFrom(this);
	opt.rect = rect();
	style()->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &painter, this);
	/* draw the spinner animation */
	renderer->render(&painter, contentsRect().adjusted(4, 4, -4, -4));
}
