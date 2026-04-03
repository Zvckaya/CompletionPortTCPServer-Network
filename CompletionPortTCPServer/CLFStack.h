#pragma once
#include <Windows.h>
#include "CDebugLog.h"
#include "CLFMemoryPool.h"

template<typename T>
class CLFStack
{
public:
	struct Node
	{
		T     data;
		Node* next;
	};

	explicit CLFStack(int initialCapacity = 100)
		: _top(0), _length(0), _nodePool(initialCapacity, false)
	{}

	~CLFStack()
	{
		Node* cur = UnpackPtr((UINT64)_top);
		while (cur != nullptr)
		{
			Node* temp = cur;
			cur = cur->next;
			_nodePool.Free(temp);
		}
	}

	CLFStack(const CLFStack&) = delete;
	CLFStack& operator=(const CLFStack&) = delete;

	int  GetLength();
	void Push(const T& data);
	bool Pop(T& data);

private:
	using BlockNode = typename CLFMemoryPool<Node>::st_BLOCK_NODE;

	// x64 Windows 유저 공간 주소는 상위 17비트가 항상 0
	// → 그 자리에 스탬프 카운터를 pack해서 ABA 감지
	static constexpr UINT64 ADDR_MASK   = 0x00007FFFFFFFFFFF;  // 하위 47비트: 실제 주소
	static constexpr int    STAMP_SHIFT = 47;
	static constexpr UINT64 STAMP_MASK  = 0x1FFFF;             // 상위 17비트: 스탬프 (max ~131072)

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

	// Node* → st_BLOCK_NODE* 역산
	// 침습성 리스트 구조: st_BLOCK_NODE::data 필드가 Node를 감싸고 있으므로
	// offsetof로 헤더 위치를 복원
	static BlockNode* GetBlockNode(Node* node)
	{
		return reinterpret_cast<BlockNode*>(
			reinterpret_cast<uintptr_t>(node) - offsetof(BlockNode, data)
		);
	}

	__declspec(align(8)) volatile LONGLONG _top;   // packed: [17-bit stamp | 47-bit ptr]
	volatile int        _length;
	CLFMemoryPool<Node> _nodePool;
};


template<typename T>
int CLFStack<T>::GetLength()
{
	return _length;
}

template<typename T>
void CLFStack<T>::Push(const T& data)
{
	Node* node = _nodePool.Alloc();

	// 할당 직후 검증: 풀이 CODE_ALLOC으로 마킹했는지 확인
	if (GetBlockNode(node)->checkCode != CODE_ALLOC)
		__debugbreak();

	node->data = data;
	//Log_Record(eLogEvent::PUSH_NEW, node);

	LONGLONG oldPacked;
	LONGLONG newPacked;
	do {
		oldPacked     = _top;
		Node*  oldTop = UnpackPtr((UINT64)oldPacked);
		UINT64 stamp  = UnpackStamp((UINT64)oldPacked);
		node->next    = oldTop;
		newPacked     = Pack(node, stamp + 1);
		//Log_Record(eLogEvent::PUSH_CAS_BEFORE, oldTop, node);
	} while (InterlockedCompareExchange64(&_top, newPacked, oldPacked) != oldPacked);

	Log_Record(eLogEvent::PUSH_CAS_OK, node, node->next);
	InterlockedIncrement((volatile LONG*)&_length);
}

template<typename T>
bool CLFStack<T>::Pop(T& data)
{
	LONGLONG oldPacked;
	LONGLONG newPacked;
	Node*    oldTop;
	Node*    newTop;
	do
	{
		oldPacked = _top;
		oldTop    = UnpackPtr((UINT64)oldPacked);
		if (oldTop == nullptr)
			return false;

		if (GetBlockNode(oldTop)->checkCode != CODE_ALLOC)
			__debugbreak();

		newTop        = oldTop->next;
		UINT64 stamp  = UnpackStamp((UINT64)oldPacked);
		newPacked     = Pack(newTop, stamp + 1);
		Log_Record(eLogEvent::POP_CAS_BEFORE, oldTop, newTop);
	} while (InterlockedCompareExchange64(&_top, newPacked, oldPacked) != oldPacked);

	//Log_Record(eLogEvent::POP_CAS_OK, newTop, oldTop);

	data = oldTop->data;

	//Log_Record(eLogEvent::POP_DELETE, oldTop);
	_nodePool.Free(oldTop);   

	InterlockedDecrement((volatile LONG*)&_length);
	return true;
}
