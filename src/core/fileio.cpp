#include "fileio.h"

#include <QFileDialog>
#include <QMap>
#include <QSvgGenerator>
#include <QBuffer>
//#include <QPrinter> // for PDF support
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>

QString FileIO::chooseFile(FileIO::Role purpose, QWidget *window)
{
	static const QMap<Role, RoleDef> map = {
	    {OpenDataset, {"Open Dataset", "Peak Volumes Table (*.tsv *.txt);; All Files (*)", false, {}}},
	    {OpenDescriptions, {"Open Descriptions", "Two-column table with descriptions (*.tsv *.txt);; All Files (*)", false, {}}},
	    {OpenStructure, {"Open Annotations or Clustering",
	                     "All supported files (*.tsv *.txt *.json);; "
	                     "Annotation Table / Protein Lists (*.tsv *.txt);; Hierarchical Clustering (*.json);; "
	                     "All Files (*)",
	                      false, {}}},
	    {OpenMarkers, {"Open Markers List", "List of markers (*.txt);; All Files (*)", false, {}}},
	    {OpenComponents, {"Open Component Table", "Profile component table (*.tsv);; All Files (*)", false, {}}},
	    {OpenProject, {"Open Project File", "Belki Project File (*.belki)", false, ".belki"}},
	    {SaveMarkers, {"Save Markers to File", "List of markers (*.txt)", true, ".txt"}},
	    {SaveAnnotations, {"Save Annotations to File", "Annotation table (*.tsv)", true, ".tsv"}},
	    //with pdf//{SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Document Format (*.pdf);; Portable Network Graphics (*.png)", true, {}}},
	    {SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Network Graphics (*.png)", true, {}}},
	    {SaveProject, {"Save Project to File", "Belki Project File (*.belki)", true, ".belki"}},
	};

	auto params = map[purpose];
	if (params.isWrite) {
		auto filename = QFileDialog::getSaveFileName(window, params.title, {}, params.filter);
		if (!params.writeSuffix.isEmpty() && !filename.isEmpty() &&
		    QFileInfo(filename).suffix().isEmpty())
			filename.append(params.writeSuffix);
		return filename;
	}

	return QFileDialog::getOpenFileName(window, params.title, {}, params.filter);
}

template<typename Q>
void render(Q *source, QPaintDevice *target) {
	QPainter p;
	p.begin(target);
	p.setRenderHints(QPainter::Antialiasing);
	source->render(&p);
	p.end();
};

template<typename Q>
QPixmap pixmaprender(Q *source, const QRectF &rect) {
	const qreal scale = 1.; // 2.; // render in higher resolution
	QPixmap target((rect.size()*scale).toSize());
	target.fill(Qt::transparent);
	target.setDevicePixelRatio(scale);
	render(source, &target);
	return target;
};

template<typename Q>
void svgrender(Q *source, QSvgGenerator *dest, const QRectF &rect, int dpi,
               const FileIO::RenderMeta &meta = {}) {
	dest->setSize(rect.size().toSize());
	dest->setViewBox(rect);
	dest->setTitle(meta.title);
	dest->setDescription(meta.description);
	dest->setResolution(dpi);
	render(source, dest);
};

template<typename Q>
void filerender(Q *source, QRectF rect, int dpi, const QString &filename, FileIO::FileType filetype,
                const FileIO::RenderMeta &meta)
{
	switch (filetype) {
	case FileIO::FileType::SVG: {
		QSvgGenerator svg;
		svg.setFileName(filename);
		svgrender(source, &svg, rect, dpi, meta);
	} break;
	case FileIO::FileType::PDF: { // TODO: this produces only a bitmap, so we disabled it for now
		// maybe use QPicture trick. also need to adapt page size
		/*QPrinter pdf;
		pdf.setOutputFormat(QPrinter::PdfFormat);
		pdf.setOutputFileName(filename);
		renderer(&pdf);*/
	} break;
	case FileIO::FileType::RASTERIMG: {
		pixmaprender(source, rect).save(filename);
	}
	}
}

QWidget *getParent(QGraphicsView *v, QGraphicsScene *s)
{
	QWidget *parent = nullptr;
	if (v)
		parent = v->window();
	if (s)
		parent = s->views().first()->window();
	if (!parent)
		std::runtime_error("renderTo*() called with invalid source object!");
	return parent;
}

void FileIO::renderToFile(QObject *source, const RenderMeta &meta, QString filename)
{
	// note: this method can easily be augmented with support for QWidget* instead of these:
	auto *v = qobject_cast<QGraphicsView*>(source);
	auto *s = qobject_cast<QGraphicsScene*>(source);
	QWidget *parent = getParent(v, s);

	if (filename.isEmpty())
		filename = chooseFile(SavePlot, parent);
	if (filename.isEmpty())
		return;

	auto suffix = QFileInfo(filename).suffix().toLower();
	if (suffix.isEmpty()) {
		emit message({"Please select a filename with suffix (e.g. .svg)!"});
		return;
	}
	auto filetype = filetypes.find(suffix);
	if (filetype == filetypes.end()) {
		emit message({"Unsupported file type (filename suffix) specified!"});
		return;
	}

	if (v) {
		auto b = v->backgroundBrush();
		v->setBackgroundBrush(QBrush(Qt::BrushStyle::NoBrush));
		filerender(v, v->contentsRect(), parent->logicalDpiX(), filename, filetype->second, meta);
		v->setBackgroundBrush(b);
	}
	if (s)
		filerender(s, s->sceneRect(), parent->logicalDpiX(), filename, filetype->second, meta);
}

void FileIO::renderToClipboard(QObject *source)
{
	auto *v = qobject_cast<QGraphicsView*>(source);
	auto *s = qobject_cast<QGraphicsScene*>(source);
	QWidget *parent = getParent(v, s);

	QPixmap pixmap;
	QBuffer svgbuffer;
	QSvgGenerator svg;
	svg.setOutputDevice(&svgbuffer);

	if (v) {
		auto b = v->backgroundBrush();
		v->setBackgroundBrush(QBrush(Qt::BrushStyle::NoBrush));
		svgrender(v, &svg, v->contentsRect(), parent->logicalDpiX());
		pixmap = pixmaprender(v, v->contentsRect());
		v->setBackgroundBrush(b);
	}
	if (s) {
		svgrender(s, &svg, s->sceneRect(), parent->logicalDpiX());
		pixmap = pixmaprender(s, s->sceneRect());
	}

	auto clipboard = QGuiApplication::clipboard();
	auto package = new QMimeData;
	package->setImageData(pixmap);
	package->setData("image/svg+xml", svgbuffer.buffer());
	clipboard->setMimeData(package);
	// TODO: notification?
}
