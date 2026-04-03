#pragma once
#include <Windows.h>
#include "CDebugLog.h"

template<typename T>
class CLFStack
{
public:
	struct Node
	{
		T data;
		Node* next;
	};

	CLFStack() : _top(0), _length(0) {}

	~CLFStack()
	{
		Node* cur = UnpackPtr((UINT64)_top);
		while (cur != nullptr)
		{
			Node* temp = cur;
			cur = cur->next;
			delete temp;
		}
	}

	CLFStack(const CLFStack&) = delete;
	CLFStack& operator=(const CLFStack&) = delete;

	int  GetLength();
	void Push(const T& data);
	bool Pop(T& data);

private:
	static constexpr UINT64 ADDR_MASK   = 0x00007FFFFFFFFFFF;  
	static constexpr int    STAMP_SHIFT = 47;
	static constexpr UINT64 STAMP_MASK  = 0x1FFFF;             

	static LONGLONG Pack(Node* ptr, UINT64 stamp)
	{
		return (LONGLONG)(
			((stamp & STAMP_MASK) << STAMP_SHIFT) |
			(reinterpret_cast<UINT64>(ptr) & ADDR_MASK)
		);
	}

	static Node* UnpackPtr(UINT64 packed)
	{
		return reinterpret_cast<Node*>(packed & ADDR_MASK);
	}

	static UINT64 UnpackStamp(UINT64 packed)
	{
		return (packed >> STAMP_SHIFT) & STAMP_MASK;
	}

	__declspec(align(8)) volatile LONGLONG _top;   // packed: [17-bit stamp | 47-bit ptr]
	volatile int _length;
};

template<typename T>
int CLFStack<T>::GetLength()
{
	return _length;
}

template<typename T>
void CLFStack<T>::Push(const T& data)
{
	Node* node = new Node;
	node->data = data;
	Log_Record(eLogEvent::PUSH_NEW, node);

	LONGLONG oldPacked;
	LONGLONG newPacked;
	do {
		oldPacked  = _top;
		Node*  oldTop = UnpackPtr((UINT64)oldPacked);
		UINT64 stamp  = UnpackStamp((UINT64)oldPacked);
		node->next = oldTop;
		newPacked  = Pack(node, stamp + 1);
		//Log_Record(eLogEvent::PUSH_CAS_BEFORE, oldTop, node);
	} while (InterlockedCompareExchange64(&_top, newPacked, oldPacked) != oldPacked);

	//Log_Record(eLogEvent::PUSH_CAS_OK, node, node->next);

	InterlockedIncrement((volatile LONG*)&_length);
}

template<typename T>
bool CLFStack<T>::Pop(T& data)
{
	LONGLONG oldPacked;
	Node*    oldTop;
	Node*    newTop;
	do
	{
		oldPacked    = _top;
		oldTop       = UnpackPtr((UINT64)oldPacked);
		if (oldTop == nullptr)
			return false;

		newTop        = oldTop->next;
		UINT64 stamp  = UnpackStamp((UINT64)oldPacked);
		LONGLONG newPacked = Pack(newTop, stamp + 1);
		Log_Record(eLogEvent::POP_CAS_BEFORE, oldTop, newTop);
	} while (InterlockedCompareExchange64(&_top, newPacked, oldPacked) != oldPacked);

	//Log_Record(eLogEvent::POP_CAS_OK, newTop, oldTop);

	data = oldTop->data;

	//Log_Record(eLogEvent::POP_DELETE, oldTop);
	delete oldTop;

	InterlockedDecrement((volatile LONG*)&_length);

	return true;
}
