/// job_manager.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "job_manager.h"

JobManager* g_pJobManager = nullptr;

/// SetThreadName
void SetThreadName(const char threadName[]) {
	struct THREADNAME_INFO {
		DWORD dwType = 0x1000;
		LPCSTR szName = nullptr;
		DWORD dwThreadID = GetCurrentThreadId();
		DWORD dwFlags = 0;
	} thread_info;
	thread_info.szName = threadName;
	thread_info.dwThreadID = GetCurrentThreadId();
	thread_info.dwFlags = 0;

	__try {
		RaiseException(0x406D1388, 0, sizeof(thread_info) / sizeof(ULONG_PTR), (ULONG_PTR*)&thread_info);
	} __except (EXCEPTION_CONTINUE_EXECUTION) {
	}
}

/// ThreadMain
DWORD WINAPI ThreadMain(LPVOID lpParam) {
	const DWORD threadId = GetThreadId(GetCurrentThread());
	blk::log("Thread created with id %d", threadId);

	const std::string threadName = "blk_engine Thread" + std::to_string(threadId);
	SetThreadName(threadName.c_str());

	JobManager* const jobManager = (JobManager*)lpParam;
	while (jobManager->IsShuttingDown() == false) {
		Job* newJob = jobManager->GrabJob();

		if (newJob != nullptr) {
			newJob->Run();
			newJob->MarkJobAsComplete();
		}
	}

	return 0;
};

/// JobManager::JobManager
JobManager::JobManager() :
	m_JobQueueHead(nullptr),
	m_JobQueueTail(nullptr),
	m_bShutdownRequested(false) {
	g_pJobManager = this;

	m_Mutex = CreateMutex(nullptr, FALSE, nullptr);

	MemoryBarrier();

	for (int i = 0; i < MAX_NUM_THREADS; i++) {
		m_Threads[i] = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
	}
}

/// JobManager::~JobManager
JobManager::~JobManager() {
	m_bShutdownRequested = true;

	WaitForMultipleObjects(MAX_NUM_THREADS, m_Threads, TRUE, INFINITE);

	for (int i = 0; i < MAX_NUM_THREADS; i++) {
		CloseHandle(m_Threads[i]);
	}

	CloseHandle(m_Mutex);
}

/// JobManager::RegisterJob
void JobManager::RegisterJob(Job* job) {
	job->m_bIsFinished = false;

	WaitForSingleObject(m_Mutex, INFINITE);

	if (m_JobQueueHead == nullptr) {
		m_JobQueueHead = job;
		m_JobQueueTail = job;
		job->m_Next = nullptr;
	} else {
		m_JobQueueTail->m_Next = job;
		m_JobQueueTail = job;
		m_JobQueueTail->m_Next = nullptr;
	}

	ReleaseMutex(m_Mutex);
}

/// JobManager::GrabJob
Job* JobManager::GrabJob() {
	WaitForSingleObject(m_Mutex, INFINITE);

	Job* returnedJob = m_JobQueueHead;

	if (returnedJob != nullptr) {

		m_JobQueueHead = returnedJob->m_Next;

		if (m_JobQueueHead == nullptr) {
			m_JobQueueTail = nullptr;
		}
	}

	ReleaseMutex(m_Mutex);

	return returnedJob;
}
