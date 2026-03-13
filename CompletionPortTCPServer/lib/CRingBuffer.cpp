#include "CRingBuffer.h"
#include <string.h>
#include <algorithm>

CRingBuffer::CRingBuffer(void)
	: m_pBuffer(nullptr), m_iBufferSize(0), m_iFront(0), m_iRear(0)
{
	Resize(16384);
}

CRingBuffer::CRingBuffer(int iBufferSize)
	: m_pBuffer(nullptr), m_iBufferSize(0), m_iFront(0), m_iRear(0)
{
	Resize(iBufferSize);
}

CRingBuffer::~CRingBuffer(void)
{
	if (m_pBuffer != nullptr)
	{
		delete[] m_pBuffer;
		m_pBuffer = nullptr;
	}
}

void CRingBuffer::Resize(int size)
{

	if (size <= 0) return;

	char* newBuffer = new char[size];


	if (m_pBuffer != nullptr)
	{
		int useSize = GetUseSize();


		int copySize = (std::min)(useSize, size - 1);

		if (copySize > 0)
		{
			Peek(newBuffer, copySize);
		}

		m_iFront = 0;
		m_iRear = copySize;

		delete[] m_pBuffer;
	}
	else
	{
		m_iFront = 0;
		m_iRear = 0;
	}

	m_pBuffer = newBuffer;
	m_iBufferSize = size;
}

int CRingBuffer::GetBufferSize(void)
{
	return m_iBufferSize;
}

int CRingBuffer::GetUseSize(void)
{
	if (m_iRear >= m_iFront)
	{
		return m_iRear - m_iFront;
	}
	else
	{
		return (m_iBufferSize - m_iFront) + m_iRear;
	}
}

int CRingBuffer::GetFreeSize(void)
{
	int useSize = GetUseSize();
	return (m_iBufferSize - 1) - useSize;
}

int CRingBuffer::Enqueue(const char* chpData, int iSize)
{
	int freeSize = GetFreeSize();

	if (iSize > freeSize)
	{
		iSize = freeSize;
	}

	if (iSize <= 0) return 0;

	int linearFreeSize = m_iBufferSize - m_iRear;

	if (iSize <= linearFreeSize)
	{
		memcpy(&m_pBuffer[m_iRear], chpData, iSize);
	}
	else
	{
		memcpy(&m_pBuffer[m_iRear], chpData, linearFreeSize);
		memcpy(&m_pBuffer[0], chpData + linearFreeSize, iSize - linearFreeSize);
	}

	m_iRear = (m_iRear + iSize) % m_iBufferSize;

	return iSize;
}

int CRingBuffer::Dequeue(char* chpDest, int iSize)
{
	int useSize = GetUseSize();

	if (iSize > useSize)
	{
		iSize = useSize;
	}

	if (iSize <= 0) return 0;

	int linearUseSize = m_iBufferSize - m_iFront;

	if (iSize <= linearUseSize)
	{
		memcpy(chpDest, &m_pBuffer[m_iFront], iSize);
	}
	else
	{
		memcpy(chpDest, &m_pBuffer[m_iFront], linearUseSize);
		memcpy(chpDest + linearUseSize, &m_pBuffer[0], iSize - linearUseSize);
	}

	m_iFront = (m_iFront + iSize) % m_iBufferSize;

	return iSize;
}

int CRingBuffer::Peek(char* chpDest, int iSize)
{
	int useSize = GetUseSize();

	if (iSize > useSize)
	{
		iSize = useSize;
	}

	if (iSize <= 0) return 0;

	int linearUseSize = m_iBufferSize - m_iFront;

	if (iSize <= linearUseSize)
	{
		memcpy(chpDest, &m_pBuffer[m_iFront], iSize);
	}
	else
	{
		memcpy(chpDest, &m_pBuffer[m_iFront], linearUseSize);
		memcpy(chpDest + linearUseSize, &m_pBuffer[0], iSize - linearUseSize);
	}

	return iSize;
}

void CRingBuffer::ClearBuffer(void)
{
	m_iFront = 0;
	m_iRear = 0;
}

int CRingBuffer::DirectEnqueueSize(void)
{
	if (m_iRear >= m_iFront)
	{
		int size = m_iBufferSize - m_iRear;
		if (m_iFront == 0)
		{
			return size - 1;
		}
		return size;
	}

	else
	{
		return (m_iFront - m_iRear) - 1;
	}
}

int CRingBuffer::DirectDequeueSize(void)
{
	if (m_iRear >= m_iFront)
	{
		return m_iRear - m_iFront;
	}

	else
	{
		return m_iBufferSize - m_iFront;
	}
}

int CRingBuffer::MoveRear(int iSize)
{
	m_iRear = (m_iRear + iSize) % m_iBufferSize;
	return iSize;
}

int CRingBuffer::MoveFront(int iSize)
{
	m_iFront = (m_iFront + iSize) % m_iBufferSize;
	return iSize;
}

char* CRingBuffer::GetFrontBufferPtr(void)
{
	return &m_pBuffer[m_iFront];
}

char* CRingBuffer::GetRearBufferPtr(void)
{
	return &m_pBuffer[m_iRear];
}

char* CRingBuffer::GetBufferPtr(void)
{
	return m_pBuffer;
}
