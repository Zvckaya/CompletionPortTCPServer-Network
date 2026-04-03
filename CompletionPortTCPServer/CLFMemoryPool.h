#pragma once
#include <Windows.h>
#include <new>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <iostream>

#define CODE_ALLOC 0x99999999
#define CODE_FREE  0xDEADDEAD

template<class DATA>
class CLFMemoryPool
{
public:
	struct st_BLOCK_NODE
	{
		CLFMemoryPool<DATA>* pOwner;
		unsigned int         checkCode;
		st_BLOCK_NODE*       next;
		bool                 isConstructed;

		alignas(DATA) unsigned char data[sizeof(DATA)];
	};

	CLFMemoryPool(int iBlockNum, bool bPlacementNew = false)
		: m_iCapacity(0), m_iUseCount(0)
		, m_bPlacementNew(bPlacementNew)
		, _pFreeNode(0)
	{
		// 생성자는 단일 스레드 → CAS 없이 직접 구성
		st_BLOCK_NODE* pHead = nullptr;
		for (int i = 0; i < iBlockNum; ++i)
		{
			st_BLOCK_NODE* pNode  = new st_BLOCK_NODE;
			pNode->pOwner         = this;
			pNode->checkCode      = CODE_FREE;
			pNode->isConstructed  = false;
			pNode->next           = pHead;
			pHead                 = pNode;
			m_iCapacity++;
		}
		_pFreeNode = Pack(pHead, 0);
	}

	~CLFMemoryPool()
	{
		st_BLOCK_NODE* cur = UnpackPtr((UINT64)_pFreeNode);
		while (cur != nullptr)
		{
			st_BLOCK_NODE* pDel = cur;
			cur = cur->next;

			if (pDel->isConstructed)
				reinterpret_cast<DATA*>(pDel->data)->~DATA();

			delete pDel;
		}
	}

	CLFMemoryPool(const CLFMemoryPool&) = delete;
	CLFMemoryPool& operator=(const CLFMemoryPool&) = delete;

	template<typename... Args>
	DATA* Alloc(Args&&... args)
	{
		st_BLOCK_NODE* pNode = nullptr;

		// free-list에서 CAS로 pop
		LONGLONG oldPacked;
		LONGLONG newPacked;
		do {
			oldPacked = _pFreeNode;
			pNode     = UnpackPtr((UINT64)oldPacked);

			if (pNode == nullptr)
			{
				// 풀 소진 → resize (new는 여기서만 발생)
				pNode                = new st_BLOCK_NODE;
				pNode->pOwner        = this;
				pNode->checkCode     = CODE_FREE;
				pNode->isConstructed = false;
				InterlockedIncrement((volatile LONG*)&m_iCapacity);
				break;
			}

			UINT64 stamp = UnpackStamp((UINT64)oldPacked);
			newPacked    = Pack(pNode->next, stamp + 1);
		} while (InterlockedCompareExchange64(&_pFreeNode, newPacked, oldPacked) != oldPacked);

		if (pNode->checkCode == CODE_ALLOC)
		{
			std::cout << "이중 할당 시도\n";
			return nullptr;
		}

		pNode->checkCode = CODE_ALLOC;
		InterlockedIncrement((volatile LONG*)&m_iUseCount);

		DATA* pRet = reinterpret_cast<DATA*>(pNode->data);

		if (!pNode->isConstructed)
		{
			new (pRet) DATA(std::forward<Args>(args)...);
			pNode->isConstructed = true;
			return pRet;
		}

		if (m_bPlacementNew)
			new (pRet) DATA(std::forward<Args>(args)...);

		return pRet;
	}

	bool Free(DATA* pData)
	{
		if (pData == nullptr)
			return false;

		uintptr_t      nodeAddr = reinterpret_cast<uintptr_t>(pData) - offsetof(st_BLOCK_NODE, data);
		st_BLOCK_NODE* pNode    = reinterpret_cast<st_BLOCK_NODE*>(nodeAddr);

		if (pNode->pOwner != this)
		{
			std::cout << "다른 풀 소속 노드 반납 시도\n";
			return false;
		}

		if (pNode->checkCode == CODE_FREE)
		{
			std::cout << "이중 반납 시도\n";
			return false;
		}

		if (m_bPlacementNew)
			pData->~DATA();

		pNode->checkCode = CODE_FREE;

		// free-list에 CAS로 push
		LONGLONG oldPacked;
		LONGLONG newPacked;
		do {
			oldPacked    = _pFreeNode;
			UINT64 stamp = UnpackStamp((UINT64)oldPacked);
			pNode->next  = UnpackPtr((UINT64)oldPacked);
			newPacked    = Pack(pNode, stamp + 1);
		} while (InterlockedCompareExchange64(&_pFreeNode, newPacked, oldPacked) != oldPacked);

		InterlockedDecrement((volatile LONG*)&m_iUseCount);
		return true;
	}

	int GetCapacityCount() const { return m_iCapacity; }
	int GetUseCount()      const { return m_iUseCount; }

private:
	// x64 유저 공간 상위 17비트를 스탬프로 사용 — ABA 감지
	static constexpr UINT64 ADDR_MASK   = 0x00007FFFFFFFFFFF;
	static constexpr int    STAMP_SHIFT = 47;
	static constexpr UINT64 STAMP_MASK  = 0x1FFFF;

	static LONGLONG Pack(st_BLOCK_NODE* ptr, UINT64 stamp)
	{
		return (LONGLONG)(
			((stamp & STAMP_MASK) << STAMP_SHIFT) |
			(reinterpret_cast<UINT64>(ptr) & ADDR_MASK)
		);
	}

	static st_BLOCK_NODE* UnpackPtr(UINT64 packed)
	{
		return reinterpret_cast<st_BLOCK_NODE*>(packed & ADDR_MASK);
	}

	static UINT64 UnpackStamp(UINT64 packed)
	{
		return (packed >> STAMP_SHIFT) & STAMP_MASK;
	}

	volatile long    m_iCapacity;
	volatile long    m_iUseCount;
	bool             m_bPlacementNew;

	__declspec(align(8)) volatile LONGLONG _pFreeNode;   // packed: [17-bit stamp | 47-bit ptr]
};
