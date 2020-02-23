# QZip

A simple C++/Qt wrapper around miniz. The miniz sources are untouched and need to be added to a project (miniz.c, miniz.h) in addition to qzip.h.

We only wrap the functionality to process ZIP contents in memory, not for adding files on disk, or to extract to disk. However, the QIODevice interface allows to easily write/read from/to disk or other targets, like sockets.

## Limitations

Currently, when opening a ZIP archive, the whole (compressed) file data is loaded into memory.
This is a performance issue when a huge archive is loaded, but only a smaller part of the contained files is to be read.
Also when creating a new archive, it is first setup in memory (compressed) before being stored to disk when calling write().

At least for reading, this could be improved upon by implementing a reader callback (see archive_.m_pRead, similar to the currently implemented archive_.m_pWrite callback) that wraps QIODevice. The Zip object would need to keep track and differentiate between working on QIODevice or QByteArray for this. Further, switching to write/append mode would need to re-open a QIODevice to writing, or change from QFile to QSaveFile, etc.
