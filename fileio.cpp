#include "fileio.h"

#include <QMainWindow>
#include <QFileDialog>
#include <QMap>
#include <QSvgGenerator>
//#include <QPrinter> // for PDF support
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsView>

#include <QtDebug>

FileIO::FileIO(QMainWindow *parent) :
    QObject(parent), parent(parent)
{}

QString FileIO::chooseFile(FileIO::Role purpose, QWidget *p)
{
	const QMap<Role, RoleDef> map = {
	    {OpenDataset, {"Open Dataset", "Peak Volumes Table or ZIP file (*.tsv *.zip)", false, {}}},
	    {OpenDescriptions, {"Open Descriptions", "Two-column table with descriptions (*.tsv)", false, {}}},
	    {OpenClustering, {"Open Annotations or Clustering",
	                      "All supported files (*.tsv *.txt *.json);; "
	                      "Annotation Table / Protein Lists (*.tsv *.txt);; Hierarchical Clustering (*.json)",
	                      false, {}}},
	    {OpenMarkers, {"Open Markers List", "List of markers (*.txt);; All Files (*)", false, {}}},
	    {SaveMarkers, {"Save Markers to File", "List of markers (*.txt)", true, ".txt"}},
	    {SaveAnnotations, {"Save Annotations to File", "Annotation table (*.tsv)", true, ".tsv"}},
	    //with pdf//{SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Document Format (*.pdf);; Portable Network Graphics (*.png)", true, {}}},
	    {SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Network Graphics (*.png)", true, {}}},
	};

	if (!p)
		p = parent;

	auto params = map[purpose];
	if (params.isWrite) {
		auto filename = QFileDialog::getSaveFileName(p, params.title, {}, params.filter);
		if (!params.writeSuffix.isEmpty() && QFileInfo(filename).suffix().isEmpty())
			filename.append(params.writeSuffix);
		return filename;
	}

	return QFileDialog::getOpenFileName(p, params.title, {}, params.filter);
}

template<typename Q>
void render(Q *source, QRectF rect, int dpi, const QString &filename, FileIO::FileType filetype, const FileIO::RenderMeta &meta)
{
	auto renderer = [source] (QPaintDevice *target) {
		QPainter p;
		p.begin(target);
		source->render(&p);
		p.end();
	};

	switch (filetype) {
	case FileIO::FileType::SVG: {
		QSvgGenerator svg;
		svg.setFileName(filename);
		svg.setSize(rect.size().toSize());
		svg.setViewBox(rect);
		svg.setTitle(meta.title);
		svg.setDescription(meta.description);
		svg.setResolution(dpi);
		renderer(&svg);
	} break;
	case FileIO::FileType::PDF: { // TODO: this produces only a bitmap, so we disabled it for now
		// maybe use QPicture trick. also need to adapt page size
		/*QPrinter pdf;
		pdf.setOutputFormat(QPrinter::PdfFormat);
		pdf.setOutputFileName(filename);
		renderer(&pdf);*/
	} break;
	case FileIO::FileType::RASTERIMG: {
		const qreal scale = 1.; // 2.; // render in higher resolution
		QPixmap pixmap((rect.size()*scale).toSize());
		pixmap.fill(Qt::transparent);
		pixmap.setDevicePixelRatio(scale);
		renderer(&pixmap);
		pixmap.save(filename);
	}
	}
}

void FileIO::renderToFile(QObject *source, const RenderMeta &meta, QString filename)
{
	QWidget *parent = nullptr;
	auto *v = qobject_cast<QGraphicsView*>(source);
	auto *s = qobject_cast<QGraphicsScene*>(source);
	// note: this method can easily be augmented with support for QWidget*
	if (v)
		parent = v->window();
	if (s)
		parent = s->views().first()->window();
	if (!parent)
		std::runtime_error("renderToFile() called with invalid source object!");

	if (filename.isEmpty())
		filename = chooseFile(SavePlot, parent);
	if (filename.isEmpty())
		return;

	auto suffix = QFileInfo(filename).suffix().toLower();
	if (suffix.isEmpty()) {
		emit ioError("Please select a filename with suffix (e.g. .svg)!");
		return;
	}
	auto filetype = filetypes.find(suffix);
	if (filetype == filetypes.end()) {
		emit ioError("Unsupported file type (filename suffix) specified!");
		return;
	}

	if (v) {
		auto b = v->backgroundBrush();
		v->setBackgroundBrush(QBrush(Qt::BrushStyle::NoBrush));
		render(v, v->contentsRect(), parent->logicalDpiX(), filename, filetype->second, meta);
		v->setBackgroundBrush(b);
	}
	if (s)
		render(s, s->sceneRect(), parent->logicalDpiX(), filename, filetype->second, meta);
}
