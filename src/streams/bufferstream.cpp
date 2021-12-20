#include "blah/streams/bufferstream.h"
#include <string.h>

using namespace Blah;

BufferStream::BufferStream()
	: m_buffer(nullptr), m_capacity(0), m_length(0), m_position(0) {}

BufferStream::BufferStream(int capacity)
	: m_buffer(nullptr), m_capacity(0), m_length(0), m_position(0)
{
	if (capacity > 0)
	{
		m_buffer = new char[capacity];
		m_capacity = capacity;
	}
}

BufferStream::BufferStream(BufferStream&& src) noexcept
{
	m_buffer = src.m_buffer;
	m_length = src.m_length;
	m_capacity = src.m_capacity;
	m_position = src.m_position;
	src.m_buffer = nullptr;
	src.m_position = src.m_length = src.m_capacity = 0;
}

BufferStream& BufferStream::operator=(BufferStream&& src) noexcept
{
	close();

	m_buffer = src.m_buffer;
	m_length = src.m_length;
	m_capacity = src.m_capacity;
	m_position = src.m_position;
	src.m_buffer = nullptr;
	src.m_position = src.m_length = src.m_capacity = 0;

	return *this;
}

BufferStream::~BufferStream()
{
	delete[] m_buffer;
}

size_t BufferStream::length() const
{
	return m_length;
}

size_t BufferStream::position() const
{
	return m_position;
}

size_t BufferStream::seek(size_t seekTo)
{
	return m_position = (seekTo < 0 ? 0 : (seekTo > m_length ? m_length : seekTo));
}

size_t BufferStream::read_data(void* ptr, size_t len)
{
	if (m_buffer == nullptr || ptr == nullptr)
		return 0;

	if (len < 0)
		return 0;

	if (len > m_length - m_position)
		len = m_length - m_position;

	memcpy(ptr, m_buffer + m_position, (size_t)len);
	m_position += len;
	return len;
}

size_t BufferStream::write_data(const void* ptr, size_t len)
{
	if (len < 0)
		return 0;

	// resize
	if (m_position + len > m_length)
		resize(m_position + len);

	// copy data
	if (ptr != nullptr)
		memcpy(m_buffer + m_position, ptr, (size_t)len);

	// increment position
	m_position += len;

	// return the amount we wrote
	return len;
}

bool BufferStream::is_open() const
{
	return m_buffer != nullptr;
}

bool BufferStream::is_readable() const
{
	return true;
}

bool BufferStream::is_writable() const
{
	return true;
}

void BufferStream::resize(size_t length)
{
	if (m_capacity > length)
	{
		m_length = length;
	}
	else
	{
		auto last_capacity = m_capacity;

		if (m_capacity <= 0)
			m_capacity = 16;
		while (length >= m_capacity)
			m_capacity *= 2;

		char* new_buffer = new char[m_capacity];

		if (m_buffer != nullptr)
		{
			memcpy(new_buffer, m_buffer, last_capacity);
			delete[] m_buffer;
		}

		m_buffer = new_buffer;
	}

	m_length = length;
}

void BufferStream::close()
{
	delete[] m_buffer;
	m_buffer = nullptr;
	m_position = 0;
	m_length = 0;
	m_capacity = 0;
}

void BufferStream::clear()
{
	m_length = m_position = 0;
}

char* BufferStream::data()
{
	return m_buffer;
}

const char* BufferStream::data() const
{
	return m_buffer;
}
