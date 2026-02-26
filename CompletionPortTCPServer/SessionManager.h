#pragma once

class SessionManager
{
private:
	SessionManager()
	{
		InitializeSRWLock(&_lock);
		_sessionCnt = 0;
	}

	~SessionManager()
	{
	}

public:
	SessionManager(const SessionManager&) = delete;
	void operator=(const SessionManager&) = delete;

	static SessionManager& GetInstance()
	{
		static SessionManager instance;
		return instance;
	}

	void AddSession(Session* s)
	{
		AcquireSRWLockExclusive(&_lock);

		int newId = ++_sessionCnt;

		_sessions.insert({ newId, s });

		ReleaseSRWLockExclusive(&_lock);
	}

	void RemoveSession(Session* s)
	{
		AcquireSRWLockExclusive(&_lock);

		_sessions.erase(s->sessionId);

		ReleaseSRWLockExclusive(&_lock);
	}

	

private:
	SRWLOCK _lock;
	int _sessionCnt;
	std::unordered_map<int, Session*> _sessions;
};