#pragma once
#include<vector>

class CSerialBuffer
{
public:
	CSerialBuffer();
	CSerialBuffer(int size);
	CSerialBuffer(const CSerialBuffer& other);
	~CSerialBuffer();

	CSerialBuffer& operator=(const CSerialBuffer& other);
	void Clear();
	int GetBufferSize() const;
	int GetDataSize() const;
	char* GetBufferPtr();
	int GetReadPos() const;
	int GetRemainingReadSize() const;
	int MoveWritePos(int size);
	int MoveReadPos(int size);
	int PutData(const char* src, int size);
	int GetData(char* dest, int size);

	//Ã¼ÀÌ´×À» À§ÇØ¼­ & return 
	CSerialBuffer& operator<<(bool value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(int8_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(uint8_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(int16_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(uint16_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(int32_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(uint32_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(int64_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(uint64_t value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(float value) { PutData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator<<(double value) { PutData((char*)&value, sizeof(value)); return *this; }

	CSerialBuffer& operator>>(bool& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(int8_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(uint8_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(int16_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(uint16_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(int32_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(uint32_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(int64_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(uint64_t& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(float& value) { GetData((char*)&value, sizeof(value)); return *this; }
	CSerialBuffer& operator>>(double& value) { GetData((char*)&value, sizeof(value)); return *this; }


private:
	std::vector<char> _buffer;
	int _readPos;
	int _writePos;
};