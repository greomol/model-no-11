#pragma once
#define _CRT_SECURE_NO_WARNINGS 1
#include <vector>
#include <queue>
#include <set>
#include <map>
#include <string>
#include <deque>
#include <fstream>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
using namespace std;

typedef int int32;
typedef unsigned uint32;
typedef unsigned char byte;
typedef unsigned char uint8;
typedef signed char   int8;
typedef long long int64;
typedef unsigned long long uint64;
typedef vector<byte> bytevector;

enum ErrorCode {
    ErrorOK,
    ErrorNotExpectedType,
    ErrorReadAttemptOutOfBounds,
    ErrorObjectIsNULL,
    ErrorResourceNotFound,
    ErrorResourceInUse,
    ErrorPrematureEndOfStream,
    ErrorSizeTooBig,
    ErrorYetNotImplemented,
    ErrorDuplicateItems,
    ErrorItemNotFound,
    ErrorQueueIsEmpty,
    ErrorSocketError = 0x200,
    ErrorConnectionFailed,
    ErrorConnectionInUse,
    ErrorTimeOut,
};

struct Exception {
    Exception(ErrorCode _code, string _info) : code(_code), info(_info) {}
    int code;
    string info;
};

#ifdef _WIN32
#   include <windows.h>
#   define DS_THREAD_PROC DWORD WINAPI

static double rclock() { return (double)clock() / CLOCKS_PER_SEC; }

class Mutex {
public:
    enum reason { TIMEOUT, FAILED, SUCCESS };
    Mutex () { _systemMutex = ::CreateMutex(NULL, FALSE, NULL); }
    ~Mutex () {
        if (_systemMutex != NULL && _systemMutex != INVALID_HANDLE_VALUE)
        ::CloseHandle(_systemMutex);
    }
    int lock () const {
        DWORD err = ::WaitForSingleObject(_systemMutex, INFINITE);
        return translateError(err);
    }
    void unlock () const {
        ::ReleaseMutex(_systemMutex);
    }
private:
    HANDLE _systemMutex;
    int translateError(int err) const {
        return err == WAIT_OBJECT_0 ? SUCCESS : FAILED;
    }
    Mutex (const Mutex &);
    Mutex &operator= (const Mutex &);
};

class Event {
public:
    enum reason { TIMEOUT, FAILED, SUCCESS };
    Event (bool manualReset = TRUE) {
        _systemEvent = ::CreateEventW(NULL, manualReset, FALSE, NULL);
    }
    ~Event () {
        if (_systemEvent != NULL && _systemEvent != INVALID_HANDLE_VALUE) {  
            ::CloseHandle(_systemEvent);
	}
    }
    int wait (int msec = INFINITE) const {
        return translateError(::WaitForSingleObject(_systemEvent, msec));
    }
    void set () const { ::SetEvent(_systemEvent); }
    void reset () const { ::ResetEvent(_systemEvent); }
    bool isSet() const {
        return ::WaitForSingleObject(_systemEvent, 0) == WAIT_OBJECT_0;
    }
private:
    HANDLE _systemEvent;
    int translateError(int err) const {
	if (err == WAIT_TIMEOUT)
	    return TIMEOUT;
	if (err == WAIT_OBJECT_0)
	    return SUCCESS;
	return FAILED;
    }
    Event(const Event &);
    Event &operator= (const Event &);
};

class Thread
{
public:
    typedef LPTHREAD_START_ROUTINE ThreadProcPtr;
    typedef HANDLE                 ThreadHandle;
    typedef DWORD                  ThreadReturn; 
    Thread () { _handle = NULL; }
    virtual ~Thread () { kill(); }
    static ThreadHandle detach(ThreadProcPtr threadProc, void *parameter) {
        ThreadHandle handle = ::CreateThread(NULL, 0, threadProc, parameter, 0, NULL);
        return handle != INVALID_HANDLE_VALUE? handle : NULL;
    }
    void start(ThreadProcPtr threadProc, void *parameter = NULL) {
        if (_handle == (ThreadHandle)NULL)
            _handle = ::CreateThread(NULL, 0, threadProc, parameter, 0, NULL);
    }
    bool    wait(uint32 milliSeconds = 0xFFFFFFFF) {
        if (_handle == (ThreadHandle)NULL || _handle == (ThreadHandle)INVALID_HANDLE_VALUE) 
        return false;
        uint32 res = ::WaitForSingleObject(_handle, milliSeconds);
        return res == WAIT_OBJECT_0;
    }
    bool kill() { // Вернуть true, если нить была и была убита
        if (_handle == (ThreadHandle)NULL || _handle == (ThreadHandle)INVALID_HANDLE_VALUE) {
            return false;
        }
        ::TerminateThread(_handle, 0);
        _handle = (ThreadHandle)NULL;
        return true;
    }
    static void closeUnused(ThreadHandle handle) {
        if (handle != NULL && handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
    }
    static void sleep (uint32 milliSeconds) { ::Sleep(milliSeconds); }
private:
    Thread (const Thread &);
    Thread &operator= (const Thread &);
    ThreadHandle   _handle;
};

#else
#   include <pthread.h>
#   include <unistd.h>
#   define DS_THREAD_PROC void*
#   include <sys/time.h>
static double rclock() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1000000.;
} 

class Mutex {
public:
    enum reason { FAILED, SUCCESS };
    Mutex () {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutex_init(&_systemMutex, &attr);
    }
    ~Mutex () {
        pthread_mutex_destroy(&_systemMutex);
    }
    int lock () const {
        return translateError(pthread_mutex_lock(&_systemMutex));
    }
    void unlock () const {
        pthread_mutex_unlock(&_systemMutex);
    }
private:
    mutable pthread_mutex_t _systemMutex;
    int translateError(int err) const {
        return err == 0 ? SUCCESS : FAILED;
    }
    Mutex (const Mutex &);
    Mutex &operator= (const Mutex &);
};

class Event {
public:
    enum reason { FAILED, SUCCESS};
    Event (bool manualEvent = true) {
        pthread_mutex_init(&_mutex, NULL); 
        pthread_cond_init(&_systemEvent, &_attr);
        _manual = manualEvent;
        _signalled = false;
        reset();
    }
    ~Event () {
        pthread_mutex_destroy(&_mutex);
        pthread_cond_destroy(&_systemEvent);
    }
    int wait (int msec = 0xFFFFFFFF) const {
        pthread_mutex_lock(&_mutex);
        while (!_signalled) {
            int err = pthread_cond_wait(&_systemEvent, &_mutex);
            if (err != 0) return translateError(err);
        }
        if (!_manual) {
            _signalled = false;
        }
        pthread_mutex_unlock(&_mutex);
        return SUCCESS;
    }
    void set () const {
        pthread_mutex_lock(&_mutex);
        if (!_signalled) {
            _signalled = true;
            pthread_cond_signal(&_systemEvent);
        }
        pthread_mutex_unlock(&_mutex);
    }
    void reset () const {
        pthread_mutex_lock(&_mutex);
        _signalled = false;
        pthread_mutex_unlock(&_mutex);
    }
    bool isSet() const {
        bool retval = false;
        pthread_mutex_lock(&_mutex);
        retval = _signalled;
        pthread_mutex_unlock(&_mutex);
        return retval;
    }
private:
    mutable pthread_cond_t _systemEvent;
    mutable pthread_condattr_t _attr;
    mutable pthread_mutex_t _mutex;
    mutable bool _manual;
    mutable volatile bool _signalled;
    Event(const Event &);
    Event &operator= (const Event &);
    int translateError(int err) const {
        return err == 0? SUCCESS : FAILED;
    }
}; 

class Thread
{
public:
    typedef void* (*ThreadProcPtr)         (void *);
#define INVALID_HANDLE_VALUE                NULL
    typedef pthread_t*                      ThreadHandle;
    typedef void                            *ThreadReturn; 
    Thread () { _handle = NULL; }
    virtual ~Thread () { kill(); }
    static ThreadHandle detach(ThreadProcPtr threadProc, void *parameter) {
        ThreadHandle handle;
        handle = new pthread_t();
        int err = pthread_create(handle, NULL, threadProc, parameter);
        if (err == 0) {
            return handle;
        } else {
            return (ThreadHandle)NULL;
        }
    }
    void start(ThreadProcPtr threadProc, void *parameter = NULL) {
        if (_handle == (ThreadHandle)NULL) {
            _handle = new pthread_t();
            int err = pthread_create(_handle, NULL, threadProc, parameter);
            err = err;
        }
    }
    bool    wait(uint32 milliSeconds = 0xFFFFFFFF) {
        int res = pthread_join(*_handle, NULL);
        return res == 0;
    }
    bool kill() { // Вернуть true, если нить существовала и была убита
        if (_handle == (ThreadHandle)NULL) {
            return false;
        }
        pthread_cancel(*_handle);
        delete _handle;
        _handle = (ThreadHandle)NULL;
        return true;
    }
    static void closeUnused(ThreadHandle handle) { }
    static void sleep (uint32 ms) { ::usleep(ms * 1000); }
private:
    Thread (const Thread &);
    Thread &operator= (const Thread &);
    ThreadHandle   _handle;
};

#endif

class AutoReleaser {
public:
    AutoReleaser(Mutex const &mutex): _scopeMutex (mutex) { _scopeMutex.lock(); }
    ~AutoReleaser () { _scopeMutex.unlock(); }
private:
    Mutex const &_scopeMutex;
    AutoReleaser (const AutoReleaser &);
    AutoReleaser &operator= (const AutoReleaser &);
};

class MessageArg {
public:
    enum {
        IntType='A', Int64Type, StringType, BytevectorType, EOFType
    };
    MessageArg(int64 q) {
        uint64 pq = q;
	body.push_back((byte)Int64Type);
        for (int i = 0; i < 8; i++) {
            body.push_back((byte)(pq & 0xFF));
            pq >>= 8;
        }
    }
    MessageArg(int q) {
        body.push_back((byte)IntType);
        for (int i = 0; i <= 24; i+=8) {
            body.push_back((byte)((q >> i) & 0xFF));
        }
    }
    MessageArg(string const &s) {
        body.push_back((byte)StringType);
        for (size_t i = 0; i < s.size(); i++) {
            body.push_back(s[i]);
        }
        body.push_back((byte)0);
    }
    MessageArg(const char *s) {
        body.push_back((byte)StringType);
        for (size_t i = 0; s[i] != 0; i++) {
            body.push_back(s[i]);
        }
        body.push_back((byte)0);
    }
    bytevector body;
};

class Message {
public:
	Message(int from, int to, bytevector const &body) {
		this->from = from; this->to = to; this->body = body; ptr = 0;
        sendTime = deliveryTime = 0;
	}
	Message(MessageArg const &a1) {
        sendTime = deliveryTime = 0;
		from = -1; to = -1; ptr = 0; append(a1);
	}
	Message(MessageArg const &a1, MessageArg const &a2) {
        sendTime = deliveryTime = 0;
		from = -1; to = -1; ptr = 0;
		append(a1); append(a2);
	}
	Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3) {
        sendTime = deliveryTime = 0;
		from = -1; to = -1; ptr = 0;
		append(a1); append(a2); append(a3);
	}
	Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3, MessageArg const &a4) {
        sendTime = deliveryTime = 0;
		from = -1; to = -1; ptr = 0;
		append(a1); append(a2); append(a3);	append(a4);
	}
    ~Message() {}
	int from, to, ptr;
	bytevector body;
	void rewind() { ptr = 0;};
	string getString() {
		if (ptr < (int)body.size() && body[ptr] == (byte)MessageArg::StringType) {
			string ret; ptr++;
			while (ptr < (int)body.size() && body[ptr] != 0) {
				ret.push_back(body[ptr++]);
			}
			ptr++; return ret;
		}
		throw Exception(ErrorNotExpectedType, "expected string");
	}
	int getInt() {
		if (ptr+4 < (int)body.size() && body[ptr] == (byte)MessageArg::IntType) {
			unsigned ret = 0;
			ptr++;
			for (int i = 0; i <= 24; i+=8) {
				ret |= (body[ptr++] & 0xFF) << i;
			}
			return ret;
		}
		throw Exception(ErrorNotExpectedType, "expected int");
	}
	int64 getInt64() {
		if (ptr+8 < (int)body.size() && body[ptr] == (byte)MessageArg::Int64Type) {
			uint64 ret = 0; ptr++;
			for (int i = 0; i < 8; i++) {
				ret = (ret <<= 8) | (body[ptr+7-i] & 0xFF);
			}
			ptr += 8;
			return ret;
		}
		throw Exception(ErrorNotExpectedType, "expected int64");
	}
	bool operator<(Message const &oth) const {
		return deliveryTime < oth.deliveryTime;
	}
	int64 sendTime, deliveryTime;
private:
	void append(MessageArg const &a) {
		for (size_t i = 0; i < a.body.size(); i++) {
			body.push_back(a.body[i]);
		}
	}
};

/////////////////////////////////////////////
class MessageQueue {
public:
	Message dequeue() {
		AutoReleaser ar(mutex);
		Message ret = queue.top();
		queue.pop();
		return ret;
	}
	void enqueue( Message const &msg ) {
		AutoReleaser ar(mutex);
		queue.push(msg);
	}
    MessageQueue() {}
    ~MessageQueue() {}
    Message peek() const { return queue.top(); }
	int size() const { return (int)queue.size(); }
private:
	priority_queue<Message> queue;
	Mutex mutex;
};

class Random {
public:
    Random() {}
    ~Random() {}
	double nextDouble() const {	return (double)rand() / RAND_MAX; }
	int    nextInt(int max) const { return (int)rand() % max; }
    bool   nextBool() const;
	void   srand(unsigned seed) { ::srand(seed); }
private:
	mutable double nextNextGaussian;
	mutable bool haveNextNextGaussian;
};

class Process;
class World;

// Сетевая инфраструктура. Каждый процесс должен зарегистрироваться в ней.
// Она также регистрирует связи между процессами и посылает сообшения процессам.
class NetworkLayer
{
public:
	NetworkLayer() {
		asyncMode = true; tick = 0;   errorRate = 0;
		stopFlag = false;
		globalTimer.start(globalTimerExecutor, this);
	}
	~NetworkLayer() {
		stopFlag = true;
		globalTimer.kill();
	}
	// Имеется два режима моделирования - асинхронный и синхронный. 
	// В первом режиме сообщения посылаются процессу немедленно.
	// Сообщения доставляются процессу через время, указанное в свойствах связи.
	// Процесс принимает сообщения независимо от показания глобальных часов и независимо
	// от других процессов.
	// Во втором режиме сообщения принимаются всеми процессами одновременно по команде nextTick().
	// 
    void setAsyncMode() {asyncMode = true;}
    void setSyncMode() { asyncMode = false;}
    void setErrorRate(double rate) { errorRate = rate; }
	void nextTick() {
		if (asyncMode) return;
		tick++;
		for (size_t toProcess = 0; toProcess < eventsMap.size(); toProcess++) {
			eventsMap[toProcess].wakeEvent->set();
		}
	}
	int createLink(int from, int to, bool bidirectional = true, int cost = 0) {
		AutoReleaser aur(mutex); 
		networkMap[from][to] = cost;
		if (bidirectional) networkMap[to][from] = cost;
		return 0;
	}
	int getLink(int p1, int p2) const {
		if (p1 < 0 || p1 == p2) return 0;
		if (p1 >= (int)networkMap.size()) return -1;
		if (p2 >= (int)networkMap[p1].size()) return -1;
		return networkMap[p1][p2];
	}
	int send(int fromProcess, int toProcess, Message const &msg) {
		return send(fromProcess, toProcess, msg.body);
	}
	int send(int fromProcess, int toProcess, bytevector const &msg) {
		if (toProcess >= networkSize) return ErrorSizeTooBig;
		Message m(fromProcess, toProcess, msg);
		if (errorRate > 0 && randomNumberGenerator.nextDouble() < errorRate) return ErrorTimeOut;
		if (eventsMap[toProcess].wakeEvent == NULL) return ErrorItemNotFound;

		int p = getLink(fromProcess, toProcess);
		if (p < 0) return ErrorItemNotFound;
		m.sendTime = tick;
		m.deliveryTime = tick + p;
		eventsMap[toProcess].queue->enqueue(m);
		if (asyncMode) {
			eventsMap[toProcess].wakeEvent->set();
		}
		return ErrorOK;
	}
	int registerProcess(int node, Process *dp);
	struct queueEntry {
		Event const *wakeEvent;
		MessageQueue *queue;
		queueEntry() : wakeEvent(NULL), queue(NULL) {}
	};
	bool asyncMode;
	vector<queueEntry>	eventsMap;
	int	networkSize;
	double errorRate;
	Random randomNumberGenerator;
	volatile long long tick;
	int addLinksToAll(int from, bool bidirectional = true, int latency = 0) {
		AutoReleaser aur(mutex);
		size_t max_len = 0;
		for (int i = 0; i < networkSize; i++) {
			networkMap[from][i] = latency;
		}
		if (bidirectional) {
			for (int i = 0; i < networkSize; i++) {
				networkMap[i][from] = latency;
			}
		}
		return 0;
	}
	int addLinksFromAll(int to, bool bidirectional = true, int latency = 0) {
		AutoReleaser aur(mutex); 
		size_t max_len = 0;
		for (int i = 0; i < networkSize; i++) {
			networkMap[i][to] = latency;
		}
		if (bidirectional) {
			for (int i = 0; i < networkSize; i++) {
				networkMap[to][i] = latency;
			}
		}
		return 0;
	}
	int addLinksAllToAll(bool bidirectional = true, int latency = 0) {
		AutoReleaser aur(mutex);
		size_t max_len = 0;
		for (int i = 0; i < networkSize; i++) {
			addLinksFromAll(i, bidirectional, latency);
		}
		return 0;
	}
	vector<int> neibs(int from) const {
		AutoReleaser aur(mutex); 
		vector<int> ret;
		for (int i = 0; i < (int)networkMap[from].size();  i++) {
			if (networkMap[from][i] >= 0) {
				ret.push_back(i);
			}
		}
		return ret;

	}
	void clear() {
		AutoReleaser aur(mutex); 
		networkMap.clear(); 
	}
	void beSure(int newSize) {
		AutoReleaser aur(mutex); 
		int oldSize = (int)networkMap.size();
		if (newSize > oldSize) {
			networkMap.resize(networkSize);
			for (int i = 0; i < newSize; i++) {
				networkMap[i].resize(newSize);
				for (int j = (i < oldSize ? oldSize : 0); j < newSize; j++) {
					networkMap[i][j] = -1;
				}
			}
		}
	}
    void waitForStill() {}
private:
	Mutex	mutex;
	vector< vector<int> > networkMap;
	int64 sentMessages;
	int64 receivedMessages;
	Thread globalTimer;
	static DS_THREAD_PROC globalTimerExecutor(void *arg) {
		NetworkLayer *nl = (NetworkLayer *)arg;
		double start = rclock();
		while (!nl->stopFlag) {
			double cl = rclock() - start;
			nl->tick = (long long)cl;
			Thread::sleep(100);
		}
		return 0;
	}
	Mutex globalTimerMutex;
	bool stopFlag;
};


typedef int (* workFunction)(Process *context, Message m);

class Process {
public:
	Process(int _node) : workerEvent(false), node(_node), stopFlag(false) {
		workerThread.start(workerThreadExecutor, this);
	}
	~Process() {
		stopFlag = true; workerThread.wait();
	}
	Event   workerEvent;
	MessageQueue workerMessagesQueue;
	uint64	localTime;	// Метка времени 
	vector<int> neibs() {
		return networkLayer->neibs(node);
	}
	// Распределённый процесс может исполнять различные рабочие функции в зависимости от пришедшего сообщения
	// Каждая рабочая функция поддерживает свой контекст исполнения, для разных функций он может быть разным 
	int32 registerWorkFunction(string const &prefix, workFunction wf) {
		workers.push_back(wf); 	return ErrorOK;
	}
	// Для простоты реализации полагаем, что исполнительный поток пробует вызвать зарегистрированные 
	// рабочие функции. Если рабочая функция распознала сообщение, как предназначенное ей, она возвращает true.
	// Возможна ситуация, когда ни одна из рабочих функций не обработает сообщение, тогда оно пропадает.
	NetworkLayer *networkLayer;
	int			node;
	static bool isMyMessage(string const &prefix, string const &message) {
		if (prefix.size()+1 >= message.size()) return false;
		for (size_t i = 0; i < prefix.size(); i++) {
			if (prefix[i] != message[i]) return false;
		}
		return message[prefix.size()] == '_';
	}

#include "contextes.h"

private:
	Thread	workerThread;
	static DS_THREAD_PROC workerThreadExecutor(void *arg) {
		Process *dp = reinterpret_cast<Process *>(arg);
		while (!dp->stopFlag) {
            // dp->workerEvent.wait();
			// Поток обнаруживает появление сообщений в очереди принятых сообщений 
			// и что-то делает в зависимости от самого сообщения
			if (dp->workerMessagesQueue.size() > 0 && dp->networkLayer->tick >= dp->workerMessagesQueue.peek().deliveryTime) {
				// Пришло новое сообщение в рабочую очередь
				Message m = dp->workerMessagesQueue.dequeue();
				for (size_t i = 0; i < dp->workers.size(); i++) {
					if (dp->workers[i](dp, m)) break;
				}
			}
			Thread::sleep(1);
		}
		return 0;
	}
	bool		stopFlag;
	vector<workFunction> workers;
};

inline int NetworkLayer::registerProcess(int node, Process *dp) {
	dp->networkLayer = this;
	if (node >= (int)eventsMap.size()) eventsMap.resize(node+1);
	if (eventsMap[node].queue != NULL) return ErrorDuplicateItems;
	eventsMap[node].queue = &dp->workerMessagesQueue;
	eventsMap[node].wakeEvent = &dp->workerEvent;
	networkSize = (int)eventsMap.size();
	beSure(networkSize);
	return ErrorOK;
}

class World	{
public:
	World() {}
	~World() {
		for (size_t i = 0; i < processesList.size(); i++) {
			if (processesList[i] != NULL) {
				delete processesList[i];
				processesList[i] = NULL;
			}
		}
	}
	int createProcess(int node) {
		Process *p = new Process(node);
		if (node >= (int)processesList.size()) 	processesList.resize(node+1);
		processesList[node] = p;
		nl.registerProcess(node, p); 
		return node;
	}
	int assignWorkFunction(int node, string const &func) {
		if (node < 0 || node >= (int)processesList.size()) return ErrorItemNotFound;
		Process *dp = this->processesList[node];
		map<string, workFunction>::const_iterator it = associates.find(func);
		if (it == associates.end()) return ErrorItemNotFound;
		if (dp == NULL) return ErrorItemNotFound;
		dp->registerWorkFunction(func, it->second);
		return ErrorOK;
	}
	int registerWorkFunction(string const &func, workFunction wf) {
		associates.insert(make_pair(func, wf));
		return true;
	}
	vector<Process *> processesList;
	map<string, workFunction> associates;
	bool parseConfig(string const &name) {
		ifstream f;
		f.open(name.c_str());
		if (!f) return false;
		char    s[1024];
        int bidirected = 1, timeout = 0;
		while (!f.eof()) {
			f.getline(s, sizeof s - 1);
			if (strlen(s) == 0 || s[0] == ';') continue;
			char id[1024], msg[1024];
            int startprocess, endprocess, from, to, latency = 1, arg;
            if (sscanf(s, "bidirected %d", &bidirected) == 1) {
                continue;
            }
			else if (sscanf(s, "processes %d %d", &startprocess, &endprocess) == 2) {
				for (int i = startprocess; i <= endprocess; i++)	{
					createProcess(i);
				}
			} else if (sscanf(s, "link from %d to %d latency %d", &from, &to, &latency) == 3 || 
				sscanf(s, "link from %d to %d", &from, &to) == 2) { 
					nl.createLink(from, to, bidirected != 0, latency);
			} else if (sscanf(s, "link from %d to all latency %d", &from, &latency) == 2 ||
				sscanf(s, "link from %d to all", &from) == 1) {
					nl.addLinksToAll(from, bidirected != 0, latency);
			} else if (sscanf(s, "link from all to %d latency %d", &from, &latency) == 2 || 
				sscanf(s, "link from all to %d", &to) == 1) {
					nl.addLinksFromAll(from, bidirected != 0, latency);
			} else if (strcmp(s, "link from all to all") == 0 || 
				sscanf(s, "link from all to all latency %d", &latency) == 1) {
					nl.addLinksAllToAll(bidirected != 0, latency);
			} else if (sscanf(s, "setprocesses %d %d %s", &startprocess, &endprocess, id) == 3) {
				for (int i = startprocess; i <= endprocess; i++) {
					assignWorkFunction(i, id);
				}
			} else if (sscanf(s, "send from %d to %d %s %d", &from, &to, msg, &arg) == 4) {
				nl.send(from, to, Message(msg, arg));
			} else if (sscanf(s, "send from %d to %d %s", &from, &to, msg) == 3) {
				nl.send(from, to, Message(msg));
			} else if (sscanf(s, "wait %d", &timeout)) {
				Thread::sleep(timeout * 1000);
			} else if (strcmp(s, "wait all") == 0) {
				nl.waitForStill();
			} else {
				printf("unknown directive in input file: '%s'\n", s);
			}
		}
		f.close();
		return true;
	}
	NetworkLayer nl;
};
