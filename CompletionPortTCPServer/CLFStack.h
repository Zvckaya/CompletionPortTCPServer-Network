#include <Windows.h>

template<typename T>
class CLFStack
{
public:
	struct Node
	{
		T data;
		Node* next;
	};


	CLFStack() :_top(nullptr), _length(0)
	{
	};

	~CLFStack()
	{
		while (_top != nullptr)
		{
			Node* temp = _top;
			_top = temp->next;
			delete temp;
		}
	};

	CLFStack(const CLFStack&) = delete;
	CLFStack& operator=(const CLFStack&) = delete;

	int GetLength();
	void Push(const T& data);
	bool Pop(T& data);

private:
	__declspec(align(8)) Node* volatile _top;
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
	//Log_Record(eLogEvent::PUSH_NEW, node);                        // new 할당

	Node* oldTop;
	do {
		oldTop = _top;
		node->next = oldTop;
		//Log_Record(eLogEvent::PUSH_CAS_BEFORE, oldTop, node);    // CAS 직전
	} while (InterlockedCompareExchangePointer((PVOID volatile*)&_top, node, oldTop) != oldTop);

	Log_Record(eLogEvent::PUSH_CAS_OK, node, node->next);        // CAS 성공: new top=node, next=oldTop

	//InterlockedIncrement((volatile LONG*)&_length);
}

template<typename T>
bool CLFStack<T>::Pop(T& data)
{
	Node* oldtop;
	Node* newTop;
	do
	{
		oldtop = _top;
		if (oldtop == nullptr)
			return false;

		newTop = oldtop->next;
		Log_Record(eLogEvent::POP_CAS_BEFORE, oldtop, newTop);  // CAS 직전
	} while (InterlockedCompareExchangePointer((PVOID volatile*)&_top, newTop, oldtop) != oldtop);

	Log_Record(eLogEvent::POP_CAS_OK, newTop, oldtop);          // CAS 성공: new top=old->next

	data = oldtop->data;

	//Log_Record(eLogEvent::POP_DELETE, oldtop);                  // delete 직전
	delete oldtop;

	//InterlockedDecrement((volatile LONG*)&_length);

	return true;
}





