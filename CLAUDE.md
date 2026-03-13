# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Visual Studio 2022 프로젝트. MSBuild로 빌드:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'CompletionPortTCPServer\CompletionPortTCPServer.vcxproj' `
  /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
```

출력: `CompletionPortTCPServer\x64\Debug\CompletionPortTCPServer.exe`
실행 후 Enter로 TPS 모니터링, `q`로 종료.

## 아키텍처

### 계층 구조

```
IOCPEchoServer (콘텐츠 레이어)
    └─ CLanServer (네트워크 라이브러리)
```

- **`CLanServer`**: IOCP 기반 추상 서버. 직접 인스턴스화하지 않음. 게임서버는 이 클래스를 상속하여 가상함수를 구현.
- **`IOCPEchoServer`**: 현재 구현체(에코 서버). 실제 게임서버로 교체 예정.

콘텐츠 레이어는 `Session*`에 접근할 수 없고 **`SessionID`만** 노출됨. 모든 네트워크 조작은 `CLanServer`의 public 인터페이스(`SendPacket`, `Disconnect`)를 통해 수행.

### CLanServer 가상함수 인터페이스

```cpp
virtual bool OnConnectionRequest(const wchar_t* ip, int port);  // false 반환 시 연결 거부
virtual void OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId);
virtual void OnClientLeave(SessionID sessionId);
virtual void OnRecv(SessionID sessionId, CPacket* packet);
virtual void OnError(int errorCode, const wchar_t* msg);
```

### 세션 관리 (CLanServer 내부)

`Sessions _sessions[MAX_SESSION]` 고정 배열 + `std::stack<WORD> _freeIndices` free-list.

**SessionID 인코딩 (64bit):**
```
[상위 16bit: 배열 index] [하위 48bit: uniqueId]
```
- `index`: 배열 위치 (O(1) 조회)
- `uniqueId`: 슬롯 재사용 시 이전 SessionID 구분용 단조 증가 카운터

**ioCount 규칙:**
- `RecvPost`/`SendPost`가 WSA 호출 전 `InterlockedIncrement`, 완료 후 `ReleaseSession`에서 `InterlockedDecrement`
- `ioCount == 0` 도달 시 → `uniqueId = 0`, 슬롯 반환, `OnClientLeave` 호출
- `FindSession`은 `ioCount`를 증가시켜 반환 — 사용 후 **반드시** `ReleaseSession` 호출

**Lock 구조:**
- `_sessionLock` (SRWLOCK): `uniqueId` 유효성 확인 + `ioCount` 증가의 원자성 보장 (FindSession에서 shared, AcceptThread/ReleaseSession에서 exclusive). 세션 데이터(버퍼 등)는 관여하지 않음.
- `Session::lock` (CRITICAL_SECTION): 개별 세션의 sendBuffer 접근 보호.

### lib/ 라이브러리

| 파일 | 역할 |
|------|------|
| `CRingBuffer` | 수신/송신 링버퍼. `Session` 내부에 직접 포함 (포인터 아님) |
| `CPacket` | 네트워크 패킷 직렬화 버퍼. `OnRecv` 콜백에서 전달됨 |
| `CSerialBuffer` | `<<`/`>>` 연산자 기반 직렬화 버퍼 (`vector<char>` 기반, 동적 크기) |
| `CMemoryPool` | 가변인자 템플릿 메모리 풀 |

`AdditionalIncludeDirectories`에 `$(ProjectDir)lib`가 등록되어 있어 `#include "CPacket.h"`처럼 경로 없이 include 가능.

### 상수 (Types.h)

- `MAX_SESSION 1000`: 고정 배열 크기 (= 최대 동시 접속자 수)
- `BUFSIZE 16384`: 세션당 recv/send 링버퍼 크기
- `SERVERPORT 6000`: 기본 포트
