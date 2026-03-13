#include "CSerialBuffer.h"
#include <cstring>
#include <algorithm>
#include <cassert>

CSerialBuffer::CSerialBuffer() :_readPos(0), _writePos(0)
{
	_buffer.reserve(1024);
}

CSerialBuffer::~CSerialBuffer()
{
}
CSerialBuffer::CSerialBuffer(int size) :_readPos(0), _writePos(0)
{
	_buffer.reserve(size);

}
CSerialBuffer::CSerialBuffer(const CSerialBuffer& other)
{
	*this = other;
}


CSerialBuffer& CSerialBuffer::operator=(const CSerialBuffer& other)
{
	if (this != &other)
	{
		_buffer = other._buffer;
		_readPos = other._readPos;
		_writePos = other._writePos;
	}
	return *this;
}

void CSerialBuffer::Clear()
{
	_writePos = 0;
	_readPos = 0;
	_buffer.clear();
}

int CSerialBuffer::GetBufferSize() const
{
	return static_cast<int>(_buffer.capacity());
}

int CSerialBuffer::GetDataSize() const
{
	return _writePos;
}

char* CSerialBuffer::GetBufferPtr()
{
	return _buffer.data();
}

int CSerialBuffer::GetReadPos() const
{
	return _readPos;
}

int CSerialBuffer::GetRemainingReadSize() const
{
	return _writePos - _readPos;
}

int CSerialBuffer::MoveWritePos(int size)
{
	if (size <= 0) return _writePos;

	if (_writePos + size > static_cast<int>(_buffer.size()))
	{
		_buffer.resize(_writePos + size);
	}
	_writePos += size;
	return _writePos;

}

int CSerialBuffer::MoveReadPos(int size)
{
	if (size <= 0) return _readPos;

	if (_readPos + size > _writePos)
	{
		_readPos = _writePos;
	}
	else {
		_readPos += size;
	}

	return _readPos;
}

int CSerialBuffer::PutData(const char* src, int size)
{
	if (size <= 0) return 0;

	if (_writePos + size > static_cast<int>(_buffer.size()))
	{
		_buffer.resize(_writePos + size);
	}
	memcpy(&_buffer[_writePos], src, size);
	_writePos += size;

	return size;
}

int CSerialBuffer::GetData(char* dest, int size)
{
	if (size <= 0) return 0;

	if (_readPos + size > _writePos)
	{
		return 0;
	}
	memcpy(dest, &_buffer[_readPos], size);
	_readPos += size;
	return size;
}