#pragma once
#include "Types.h"

class CPacket
{
public:
	CPacket() : _dataSize(0) {}

	void SetData(const char* data, int size)
	{
		memcpy(_buffer, data, size);
		_dataSize = size;
	}

	char* GetBufferPtr() { return _buffer; }
	int   GetDataSize()  const { return _dataSize; }

private:
	char _buffer[BUFSIZE];
	int  _dataSize;
};
