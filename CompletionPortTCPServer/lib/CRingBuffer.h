#pragma once


class CRingBuffer {
public:
	CRingBuffer(void);
	CRingBuffer(int iBufferSize);
	~CRingBuffer(void);

	void	Resize(int size);

	int		GetBufferSize(void);

	int		GetUseSize(void);

	int		GetFreeSize(void);

	int		Enqueue(const char* chpData, int iSize);

	int		Dequeue(char* chpDest, int iSize);

	int		Peek(char* chpDest, int iSize);

	void	ClearBuffer(void);


	int		DirectEnqueueSize(void);

	int		DirectDequeueSize(void);

	int		MoveRear(int iSize);

	int		MoveFront(int iSize);

	char* GetFrontBufferPtr(void);

	char* GetRearBufferPtr(void);

	char* GetBufferPtr(void);

private:
	char* m_pBuffer;
	int		m_iBufferSize;
	int		m_iFront;
	int		m_iRear;

};
