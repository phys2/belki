/* from: https://github.com/tfussell/miniz-cpp/ 33b4f41 on Apr 18, 2017 */

// Copyright (c) 2014-2017 Thomas Fussell, 2018+ Johannes Jordan
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, WRISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE
//
// @license: http://www.opensource.org/licenses/mit-license.php

#pragma once

#include "miniz.h"

#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QDateTime>
#include <vector>

namespace miniz_cpp {
namespace detail {

/* writer that takes QByteArray* in opaque */
std::size_t write_callback(void *opaque, mz_uint64 file_ofs, const void *pBuf, std::size_t n)
{
	auto buffer = static_cast<QByteArray*>(opaque);

	if (file_ofs + n > (unsigned)buffer->size())
		buffer->resize(file_ofs + n);

	buffer->replace(file_ofs, n, (const char*)pBuf, n);
	return n;
}

}   // namespace detail

struct zip_info
{
	mz_uint index; // zip-internal index
	QString filename;
	QDateTime timestamp;
	QString comment;
	uint16_t    create_system   = 0;
	uint16_t    create_version  = 0;
	uint16_t    extract_version = 0;
	uint16_t    flag_bits       = 0;
	std::size_t volume        = 0;
	uint32_t    internal_attr = 0;
	uint32_t    external_attr = 0;
	std::size_t header_offset = 0;
	uint32_t    crc = 0;
	std::size_t compress_size = 0;
	std::size_t file_size     = 0;

	// preserve this file when writing archive
	bool        preserve = true;
};

class zip_file
{
public:
	zip_file() : archive_() // zero-initialize
	{
		reset();
	}

	~zip_file()
	{
		reset();
	}

	void load(const QString &filename)
	{
		filename_ = filename;
		QFile f(filename);
		load(f);
	}

	void load(QIODevice &io)
	{
		if (!io.isReadable())
			io.open(QIODevice::ReadOnly);
		if (!io.isReadable())
			throw std::runtime_error("IO device not readable!");

		load(io.readAll());
	}

	void load(QByteArray bytes)
	{
		reset();
		buffer_ = bytes;
		remove_comment();
		start_read();
	}

	void save()
	{
		if (filename_.isEmpty())
			throw std::logic_error("No filename set.");

		save(filename_);
	}

	void save(const QString &filename)
	{
		filename_ = filename;
		QSaveFile f(filename_);
		save(f);
	}

	void save(QIODevice &io)
	{
		if (!io.isWritable())
			io.open(QIODevice::WriteOnly);

		if (!io.isWritable())
			throw std::runtime_error("IO device not writeable!");

		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING)
			mz_zip_writer_finalize_archive(&archive_);

		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED)
			mz_zip_writer_end(&archive_);

		if (archive_.m_zip_mode == MZ_ZIP_MODE_INVALID)
			start_read();

		append_comment();
		io.write(buffer_);
	}

	QByteArray store()
	{
		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING)
			mz_zip_writer_finalize_archive(&archive_);

		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED)
			mz_zip_writer_end(&archive_);

		if (archive_.m_zip_mode == MZ_ZIP_MODE_INVALID)
			start_read();

		append_comment();
		return buffer_;
	}

	void reset()
	{
		switch (archive_.m_zip_mode) {
		case MZ_ZIP_MODE_READING:
			mz_zip_reader_end(&archive_);
			break;
		case MZ_ZIP_MODE_WRITING:
			mz_zip_writer_finalize_archive(&archive_);
			mz_zip_writer_end(&archive_);
			break;
		case MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED:
			mz_zip_writer_end(&archive_);
			break;
		case MZ_ZIP_MODE_INVALID:
			break;
		}

		if (archive_.m_zip_mode != MZ_ZIP_MODE_INVALID) {
			throw std::runtime_error("could not reset archive");
		}

		buffer_.clear();
		meta_.clear();
		comment.clear();

		start_write();
		mz_zip_writer_finalize_archive(&archive_);
		mz_zip_writer_end(&archive_);
	}

	bool has_file(const QString &name)
	{
		try {
			find(name);
		} catch (std::out_of_range&) {
			return false;
		}
		return true;
	}

	bool has_file(const zip_info &meta)
	{
		return has_file(meta.filename);
	}

	zip_info meta(const QString &name)
	{
		return meta(find(name));
	}

	std::vector<zip_info> contents()
	{
		start_read();

		auto num_files = mz_zip_reader_get_num_files(&archive_);
		if (num_files > meta_.size()) {
			for (std::size_t i = 0; i < num_files; i++)
				meta_.push_back(meta(i));
		}

		return meta_;
	}

	QStringList names()
	{
		QStringList names;

		for (auto &info : contents())
			names << info.filename;

		return names;
	}

	QByteArray read(const zip_info &info)
	{
		std::size_t size    = info.file_size;
		auto        data    = QByteArray((int)size, Qt::Uninitialized);
		bool        success = mz_zip_reader_extract_to_mem(
		    &archive_, info.index, data.data(), size, 0);
		if (!success)
			throw std::runtime_error("file couldn't be read");

		return data;
	}

	QByteArray read(const QString &name)
	{
		return read(meta(name));
	}

	std::pair<bool, QString> test()
	{
		if (archive_.m_zip_mode == MZ_ZIP_MODE_INVALID)
			throw std::runtime_error("file not open");

		for (auto &file : contents()) {
			auto content = read(file);
			auto crc     = (mz_uint32)mz_crc32(
				MZ_CRC32_INIT,
				(const mz_uint8*)content.constData(),
				(unsigned)content.size());

			if (crc != file.crc)
				return { false, file.filename };
		}

		return { true, {} };
	}

	void write(const std::string &arcname, const QByteArray &bytes)
	{
		start_write();

		bool success = mz_zip_writer_add_mem(
		    &archive_, arcname.c_str(),
			bytes.constData(), (unsigned)bytes.size(),
			MZ_BEST_COMPRESSION);

		if (!success) {
			throw std::runtime_error("write error");
		}
	}

	void write(const zip_info &info, const QByteArray &bytes)
	{
		if (info.filename.isEmpty())
			throw std::runtime_error("must specify a filename");

		start_write();

		// note: miniz expects/writes UTF8 filenames, so we use toUtf8()

		bool success;
		if (info.timestamp.isValid() && info.timestamp > QDateTime::fromTime_t(0)) {
			time_t time = info.timestamp.toTime_t();
			success = mz_zip_writer_add_mem_ex_v2(
			            &archive_, info.filename.toUtf8(), bytes.constData(),
			            (unsigned)bytes.size(), info.comment.toUtf8(),
			            static_cast<mz_uint16>(info.comment.size()),
			            MZ_BEST_COMPRESSION, 0, 0, &time,
			            nullptr, 0, nullptr, 0);
		} else {
			success = mz_zip_writer_add_mem_ex(
			            &archive_, info.filename.toUtf8(), bytes.constData(),
			            (unsigned)bytes.size(), info.comment.toUtf8(),
			            static_cast<mz_uint16>(info.comment.size()),
			            MZ_BEST_COMPRESSION, 0, 0);
		}

		if (!success)
			throw std::runtime_error("write error");
	}

	// call if you want to replace a file from or delete it from an open archive.
	// call all discards before performing any write operations.
	// when the file mode is switched from reading to writing, discards will be honored
	void discard(const zip_info &info)
	{
		if (archive_.m_zip_mode != MZ_ZIP_MODE_READING)
			throw std::logic_error("archive not in reading mode");

		meta_[info.index].preserve = false;
	}

	QString filename() const { return filename_; }

	std::string comment;

private:
	mz_uint find(const QString &name)
	{
		start_read();

		auto index = mz_zip_reader_locate_file(&archive_, name.toUtf8(), nullptr, 0);
		if (index < 0)
			throw std::out_of_range("file not found in archive");

		return (mz_uint)index;
	}

	zip_info meta(unsigned index)
	{
		start_read();

		if (meta_.size() > index)
			return meta_[index];

		mz_zip_archive_file_stat stat;
		mz_zip_reader_file_stat(&archive_, static_cast<mz_uint>(index), &stat);

		zip_info result;

		result.index    = index;
		result.filename = stat.m_filename;
		result.comment       = stat.m_comment;
		result.compress_size = static_cast<std::size_t>(stat.m_comp_size);
		result.file_size     = static_cast<std::size_t>(stat.m_uncomp_size);
		result.header_offset = static_cast<std::size_t>(stat.m_local_header_ofs);
		result.crc = stat.m_crc32;
		result.timestamp = QDateTime::fromTime_t(stat.m_time);
		result.flag_bits         = stat.m_bit_flag;
		result.internal_attr     = stat.m_internal_attr;
		result.external_attr     = stat.m_external_attr;
		result.extract_version   = stat.m_version_needed;
		result.create_version    = stat.m_version_made_by;
		result.volume        = stat.m_file_index;
		result.create_system = stat.m_method;

		return result;
	}

	void start_read()
	{
		if (archive_.m_zip_mode == MZ_ZIP_MODE_READING)
			return;

		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING)
			mz_zip_writer_finalize_archive(&archive_);

		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED)
			mz_zip_writer_end(&archive_);

		bool success = mz_zip_reader_init_mem(&archive_, buffer_.data(), buffer_.size(), 0);
		if (!success)
			throw std::runtime_error("bad zip");
	}

	void start_write()
	{
		if (archive_.m_zip_mode == MZ_ZIP_MODE_WRITING)
			return;

		switch (archive_.m_zip_mode) {
		case MZ_ZIP_MODE_READING:
		{
			mz_zip_archive archive_copy = {}; // zero-initialized
			QByteArray buffer_cow = buffer_;

			bool success = mz_zip_reader_init_mem(&archive_copy,
			                                      buffer_cow.constData(),
			                                      (unsigned)buffer_cow.size(), 0);
			if (!success) {
				throw std::runtime_error("bad zip");
			}

			mz_zip_reader_end(&archive_);

			archive_.m_pWrite     = &detail::write_callback;
			archive_.m_pIO_opaque = &buffer_;
			buffer_.clear(); // will not affect buffer_cow / archive_copy

			if (!mz_zip_writer_init(&archive_, 0))
				throw std::runtime_error("bad zip");

			for (unsigned int i = 0; i < static_cast<unsigned int>(archive_copy.m_total_files);
			     i++) {
				if (i < meta_.size() && !meta_[i].preserve)
					continue;

				bool success = mz_zip_writer_add_from_zip_reader(&archive_, &archive_copy, i);
				if (!success)
					throw std::runtime_error("fail");
			}
			meta_.clear(); // might be outdated

			mz_zip_reader_end(&archive_copy);
			return;
		}
		case MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED:
			mz_zip_writer_end(&archive_);
			break;
		case MZ_ZIP_MODE_INVALID:
		case MZ_ZIP_MODE_WRITING:
			break;
		}

		archive_.m_pWrite     = &detail::write_callback;
		archive_.m_pIO_opaque = &buffer_;

		if (!mz_zip_writer_init(&archive_, 0))
			throw std::runtime_error("bad zip");
	}

	void append_comment()
	{
		if (!comment.empty()) {
			auto comment_length = std::min(static_cast<uint16_t>(comment.length()),
			                               std::numeric_limits<uint16_t>::max());
			buffer_[buffer_.size() - 2] = static_cast<char>(comment_length);
			buffer_[buffer_.size() - 1] = static_cast<char>(comment_length >> 8);
			std::copy(comment.begin(), comment.end(), std::back_inserter(buffer_));
		}
	}

	void remove_comment()
	{
		if (buffer_.isEmpty()) return;

		auto position = buffer_.size() - 1;

		for (; position >= 3; position--) {
			if (buffer_[position - 3] == 'P'
			    && buffer_[position - 2] == 'K'
			    && buffer_[position - 1] == '\x05'
			    && buffer_[position] == '\x06') {
				position = position + 17;
				break;
			}
		}

		if (position == 3) {
			throw std::runtime_error("didn't find end of central directory signature");
		}

		uint16_t length = static_cast<uint16_t>(buffer_[position + 1]);
		length    = static_cast<uint16_t>(length << 8) + static_cast<uint16_t>(buffer_[position]);
		position += 2;

		if (length != 0) {
			comment = std::string(buffer_.data() + position, buffer_.data() + position + length);
			buffer_.resize(buffer_.size() - length);
			buffer_[buffer_.size() - 1] = 0;
			buffer_[buffer_.size() - 2] = 0;
		}
	}

	mz_zip_archive archive_;
	std::vector<zip_info> meta_; // cache of zip_info structures
	QByteArray buffer_;
	QString filename_;
};
} // namespace miniz_cpp
