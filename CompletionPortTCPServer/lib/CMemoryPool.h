#pragma once
#include <new>
#include <utility>
#include <assert.h>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <Windows.h>

#define CODE_ALLOC 0x99999999
#define CODE_FREE 0xDEADDEAD

template<class DATA>
class CMemoryPool
{
public:
	struct st_BLOCK_NODE
	{
		CMemoryPool* pOwner; //노드를 할당한 메모리 풀 주소
		unsigned int checkCode; //현재 노드 상태
		st_BLOCK_NODE* next; //다음 노드

		bool isConstructed; //생성한적이 있는가

		//템플릿으로 넘겨받은 객체를 직접 선언하지 않고, 그 크기만큼의 메모리 공간만 잡아놈
		// 처음에는 DATA를 놨으나.그러면 생성자 호출을 무조건 해야함.
		//alignas이용
		alignas(DATA) unsigned char data[sizeof(DATA)];

	};

	CMemoryPool(int iBlockNum, bool bPlacementNew = false) :
		m_iCapacity(0),
		m_iUseCount(0),
		m_bPlacementNew(bPlacementNew),
		_pFreeNode(nullptr)
	{
		InitializeCriticalSection(&m_lock);

		for (int i = 0; i < iBlockNum; ++i)
		{
			// 여기서 new를 해도 st_BLOCK_NODE 안의 data는 단순 배열이므로
			// DATA의 생성자 호출을 막을 수 있다.
			st_BLOCK_NODE* pNode = new st_BLOCK_NODE;

			//할당자
			pNode->pOwner = this;
			pNode->checkCode = CODE_FREE; //반환됨 형태
			pNode->isConstructed = false;

			//LIFO
			pNode->next = _pFreeNode;
			_pFreeNode = pNode;

			m_iCapacity++; //실제 사용량 증가
		}
	}

	virtual ~CMemoryPool() //vtable을 이용해서 런타임에 타입 결정-> 자식 부모 소멸자 호출
	{
		while (_pFreeNode != nullptr) //정리할 객체가 있을
		{
			st_BLOCK_NODE* pDeleteNode = _pFreeNode;
			_pFreeNode = _pFreeNode->next;

			if (pDeleteNode->isConstructed) //객체가 생성된적이 있었으면 소멸자 호출
			{
				DATA* pObj = reinterpret_cast<DATA*>(pDeleteNode->data);
				pObj->~DATA();
			}

			delete pDeleteNode;
		}

		DeleteCriticalSection(&m_lock);
	}

	//가변 인자 템플릿 적용
	//몇개가 들어올지는  모르지만 일단 다 받겠다~...
	//아래의 의미는 타입이 여러개 오는 데,그걸 Args로 부르겠다 라는 의미.
	template<typename... Args>
	DATA* Alloc(Args&&... args) //타입에 맞는 변수도 여러개 오는데, 퉁처서 args라고 부르겠다
	{
		EnterCriticalSection(&m_lock);

		st_BLOCK_NODE* pNode = nullptr;

		if (_pFreeNode == nullptr) //풀이 꽉찼을때
		{
			pNode = new st_BLOCK_NODE; //새 노드 생성
			pNode->pOwner = this;
			pNode->checkCode = CODE_FREE;
			pNode->isConstructed = false;

			m_iCapacity++;//사용량 증가
		}
		else //여유 풀이 있으면
		{
			pNode = _pFreeNode;  //프리노드 갱신
			_pFreeNode = _pFreeNode->next;
		}

		if (pNode->checkCode == CODE_ALLOC)
		{
			std::cout << "프리 리스트 오염";
			LeaveCriticalSection(&m_lock);
			return nullptr;
		}

		pNode->checkCode = CODE_ALLOC;
		m_iUseCount++;

		LeaveCriticalSection(&m_lock);

		DATA* pRet = reinterpret_cast<DATA*>(pNode->data);

		if (pNode->isConstructed == false)
		{
			new (pRet) DATA(std::forward<Args>(args)...);

			pNode->isConstructed = true;

			return pRet;
		}

		if (m_bPlacementNew) //생성자만 호출
		{
			new (pRet) DATA(std::forward<Args>(args)...);
		}

		return pRet;
	}

	bool Free(DATA* pData)
	{
		if (pData == nullptr)
			return false;

		uintptr_t nodeAddr = reinterpret_cast<uintptr_t>(pData) - offsetof(st_BLOCK_NODE, data);

		st_BLOCK_NODE* pNode = reinterpret_cast<st_BLOCK_NODE*>(nodeAddr);

		if (pNode->pOwner != this)
		{
			std::cout << "다른풀 노드 해제 시도";
		}

		if (pNode->checkCode == CODE_FREE)
		{
			std::cout << "해제된 노드 해제 시도 ";
		}

		if (m_bPlacementNew)
		{
			pData->~DATA();
		}

		EnterCriticalSection(&m_lock);

		pNode->checkCode = CODE_FREE;

		pNode->next = _pFreeNode;
		_pFreeNode = pNode;

		m_iUseCount--;

		LeaveCriticalSection(&m_lock);

		return true;
	}

	int GetCapacityCount(void) { return m_iCapacity; }
	int GetUseCount(void) { return m_iUseCount; }

private:
	int m_iCapacity;
	int m_iUseCount;
	bool m_bPlacementNew;
	CRITICAL_SECTION m_lock;

public:
	st_BLOCK_NODE* _pFreeNode;
};
