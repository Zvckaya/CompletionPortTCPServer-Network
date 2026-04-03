#pragma once
#include <windows.h>
#include <intrin.h>

// ============================================================
//  Lock-Free Stack 디버그 로그 (Ring Buffer)
//
//  기록 시점:
//    PUSH_NEW        : new Node 할당 직후
//    PUSH_CAS_BEFORE : CAS 시도 직전  (루프마다)
//    PUSH_CAS_OK     : CAS 성공 직후
//    POP_CAS_BEFORE  : CAS 시도 직전  (루프마다)
//    POP_CAS_OK      : CAS 성공 직후
//    POP_DELETE      : delete 직전
//
//  ptr 의미:
//    PUSH_NEW        ptr1=할당된 노드          ptr2=-
//    PUSH_CAS_BEFORE ptr1=oldTop(expected)    ptr2=newNode(desired)
//    PUSH_CAS_OK     ptr1=new top(=newNode)   ptr2=newNode->next(=oldTop)
//    POP_CAS_BEFORE  ptr1=oldTop(expected)    ptr2=oldTop->next(desired)
//    POP_CAS_OK      ptr1=new top(=old->next) ptr2=해제 예정 노드
//    POP_DELETE      ptr1=해제할 노드          ptr2=-
// ============================================================

enum class eLogEvent : BYTE
{
    PUSH_NEW,
    PUSH_CAS_BEFORE,
    PUSH_CAS_OK,
    POP_CAS_BEFORE,
    POP_CAS_OK,
    POP_DELETE,
};

struct DebugLogEntry
{
    UINT64    tsc;       // CPU timestamp at the moment this entry was recorded (rdtsc)
    LONGLONG  seq;       // global sequence number (monotonic, survives ring wrap)
    DWORD     threadId;
    eLogEvent event;
    void* ptr1;
    void* ptr2;
};

inline constexpr LONG LOG_SIZE = 100000;

inline DebugLogEntry      g_DebugLog[LOG_SIZE];
inline volatile LONGLONG  g_LogIndex = 0;

inline void Log_Record(eLogEvent event, void* ptr1, void* ptr2 = nullptr)
{
    UINT64    tsc = __rdtsc();  // read tsc before seq to minimize timestamp inversion
    long long seq = InterlockedIncrement64(&g_LogIndex) - 1;
    long      idx = static_cast<long>(seq % LOG_SIZE);

    DebugLogEntry& e = g_DebugLog[idx];
    e.tsc = tsc;
    e.seq = seq;
    e.threadId = GetCurrentThreadId();
    e.event = event;
    e.ptr1 = ptr1;
    e.ptr2 = ptr2;
}
