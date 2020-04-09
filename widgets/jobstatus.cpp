#include "jobstatus.h"
#include "jobregistry.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolTip>

const int fps = 25;

JobStatus::JobStatus(QWidget *parent)
    : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	setMouseTracking(true);

	renderer = new QSvgRenderer(QString{":/spinner.svg"}, this);
	animator = new QTimer(this);

	renderer->setFramesPerSecond(fps);
	animator->setInterval(1000 / fps);
	animator->setSingleShot(false);
	connect(animator, &QTimer::timeout, [this] { update(); });
}

void JobStatus::addJob(unsigned id)
{
	jobs.push_back(id);
	updateJobs();
}

void JobStatus::updateJob(unsigned)
{
	// TODO update progress (currently not propagated or handled yet)
}

void JobStatus::removeJob(unsigned id)
{
	for (auto it = jobs.begin(); it != jobs.end(); ++it) {
		if (*it == id) {
			jobs.erase(it);
			break;
		}
	}
	updateJobs();
}

void JobStatus::updateJobs()
{
	auto w = height() * jobs.size();
	setMinimumWidth(w);
	setMaximumWidth(w);
	if (jobs.empty())
		animator->stop();
	else
		animator->start();
}

void JobStatus::mouseMoveEvent(QMouseEvent *event)
{
	size_t index = event->pos().x() / height();
	if (index < jobs.size()) {
		auto text = JobRegistry::get()->job(jobs[index]).name;
		QRect area({(int)index*height(), 0}, QSize(height(), height()));
		QToolTip::showText(event->globalPos(), text, this, area);
	}
}

void JobStatus::resizeEvent(QResizeEvent *event)
{
	if (event->size().height() != event->oldSize().height())
		updateJobs();
}

void JobStatus::paintEvent(QPaintEvent*)
{
	QSize areaSize(height() - 4, height() - 4);
	QPainter painter(this);
	for (size_t i = 0; i < jobs.size(); ++i) {
		QRect area({2 + (int)i*height(), 2}, areaSize);
		renderer->render(&painter, area);
	}
}
