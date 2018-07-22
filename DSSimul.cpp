#include "DSSimul.h"
#include <time.h>
////// Mutex /////////////
#if DS_OS == DS_OSWINDOWS
static double rclock() { return (double)clock() / CLOCKS_PER_SEC; }
Mutex::Mutex () {
    _systemMutex = ::CreateMutex(NULL, FALSE, NULL);
}

Mutex::~Mutex () {
    if (_systemMutex != NULL && _systemMutex != INVALID_HANDLE_VALUE)
        ::CloseHandle(_systemMutex);
}

int Mutex::lock () const {
    DWORD err = ::WaitForSingleObject(_systemMutex, INFINITE);
    return translateError(err);
}

int Mutex::lock (uint32 msec) const {
    DWORD err = ::WaitForSingleObject(_systemMutex, msec);
    return translateError(err);
}

void Mutex::unlock () const {
    ::ReleaseMutex(_systemMutex);
}

int Mutex::translateError(int err) const {
    if (err == WAIT_TIMEOUT)
        return TIMEOUT;
    if (err == WAIT_OBJECT_0)
        return SUCCESS;
    return FAILED;
}
#else
#include <unistd.h>
#include <sys/time.h>
static double rclock() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1000000.;
} 

Mutex::Mutex () {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&_systemMutex, &attr);
}

Mutex::~Mutex () {
    pthread_mutex_destroy(&_systemMutex);
}

int Mutex::lock () const {
    int err = pthread_mutex_lock(&_systemMutex);
    return translateError(err);
}

void Mutex::unlock () const {
    pthread_mutex_unlock(&_systemMutex);
}

int Mutex::translateError(int err) const {
    if (err == 0)
        return SUCCESS;
    return FAILED;
}
#endif

MessageArg::MessageArg(int64 q) {
    uint64 pq = q;
    body.push_back((byte)Int64Type);
    for (int i = 0; i < 8; i++) {
        body.push_back((byte)(pq & 0xFF));
	//printf("s[%d]=%u\n", i, pq & 0xFF);
	pq >>= 8;
    }
}

MessageArg::MessageArg(int q) {
    body.push_back((byte)IntType);
    for (int i = 0; i <= 24; i+=8) {
        body.push_back((byte)((q >> i) & 0xFF));
    }
}

MessageArg::MessageArg(string const &s)
{
    body.push_back((byte)StringType);
    for (size_t i = 0; i < s.size(); i++) {
        body.push_back(s[i]);
    }
    body.push_back((byte)0);
}

MessageArg::MessageArg(const char *s)
{
    body.push_back((byte)StringType);
    for (size_t i = 0; s[i] != 0; i++) {
        body.push_back(s[i]);
    }
    body.push_back((byte)0);
}

Message::Message(int from, int to, bytevector const &body) {
    this->from = from; this->to   = to; this->body = body;
    ptr = 0;
}

Message::Message(MessageArg const &a1) {
    from = -1; to = -1; ptr = 0;
    append(a1);
}

Message::Message(MessageArg const &a1, MessageArg const &a2) {
    from = -1; to = -1; ptr = 0;
    append(a1); append(a2);
}

Message::Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3) {
    from = -1; to = -1; ptr = 0;
    append(a1); append(a2); append(a3);
}

Message::Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3, MessageArg const &a4) {
    from = -1; to = -1; ptr = 0;
    append(a1); append(a2); append(a3);	append(a4);
}

string Message::getString() {
	if (ptr < (int)body.size() && body[ptr] == (byte)MessageArg::StringType) {
		string ret;
		ptr++;
		while (ptr < (int)body.size() && body[ptr] != 0) {
			ret.push_back(body[ptr++]);
		}
		ptr++;
		return ret;
	}
	throw Exception(ErrorNotExpectedType, "expected string");
}

int64 Message::getInt64() {
	if (ptr+8 < (int)body.size() && body[ptr] == (byte)MessageArg::Int64Type) {
		uint64 ret = 0;
		ptr++;
		for (int i = 0; i < 8; i++) {
			ret <<= 8;
			byte b = body[ptr+7-i] & 0xFF;
			ret |= b;
		}
		ptr += 8;
		return ret;
	}
	throw Exception(ErrorNotExpectedType, "expected int64");
}

int Message::getInt() {
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

void Message::append(MessageArg const &a) {
	for (size_t i = 0; i < a.body.size(); i++) {
		body.push_back(a.body[i]);
	}
}

Message MessageQueue::dequeue() {
	AutoReleaser ar(mutex);
	Message ret = queue.top();
	queue.pop();
	return ret;
}

void MessageQueue::enqueue( Message const &msg ) {
	AutoReleaser ar(mutex);
	queue.push(msg);
}

NetworkLayer::NetworkLayer()  {
	mode = ASYNC; tick = 0;   errorRate = 0;
	stopFlag = false;
	globalTimer.start(globalTimerExecutor, this);
}

NetworkLayer::~NetworkLayer() {
	stopFlag = true;
	globalTimer.kill();
}

void NetworkLayer::nextTick() {
	if (mode == ASYNC) return;
	tick++;
	for (size_t toProcess = 0; toProcess < eventsMap.size(); toProcess++) {
		eventsMap[toProcess].wakeEvent->set();
	}
}

int NetworkLayer::createLink(int from, int to, bool bidirectional, int cost) {
	AutoReleaser aur(mutex); 
	networkMap[from][to] = cost;
	if (bidirectional) {
		networkMap[to][from] = cost;
	}
	return 0;
}

int NetworkLayer::registerProcess(int node, Process *dp) {
	dp->networkLayer = this;
	if (node >= (int)eventsMap.size()) {
		eventsMap.resize(node+1);
	}
	if (eventsMap[node].queue != NULL) {
		return ErrorDuplicateItems;
	}
	eventsMap[node].queue = &dp->workerMessagesQueue;
	eventsMap[node].wakeEvent = &dp->workerEvent;
	networkSize = (int)eventsMap.size();
	beSure(networkSize);
	return ErrorOK;
}

void NetworkLayer::beSure(int newSize) {
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

vector<int> NetworkLayer::neibs(int from) const {
	AutoReleaser aur(mutex); 
	vector<int> ret;
	for (int i = 0; i < (int)networkMap[from].size();  i++) {
		if (networkMap[from][i] >= 0) {
			ret.push_back(i);
		}
	}
	return ret;
}

void NetworkLayer::clear() { 
	AutoReleaser aur(mutex); 
	networkMap.clear(); 
}

void NetworkLayer::print() const {
	AutoReleaser aur(mutex); 
	for (size_t i = 0; i < networkMap.size(); i++) {
		printf("line %2ld: ", i);
		for(int j = 0; j < (int)networkMap[i].size(); j++) {
			if (networkMap[i][j] >= 0) {
				printf("%d(%d) ", j, networkMap[i][j]);
			}
		}
		printf("\n");
	}
}

int NetworkLayer::addLinksToAll(int from, bool bidirectional, int latency)
{
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

int NetworkLayer::addLinksAllToAll(bool bidirectional, int latency)
{
	AutoReleaser aur(mutex);
	size_t max_len = 0;
	for (int i = 0; i < networkSize; i++) {
		addLinksFromAll(i, bidirectional, latency);
	}
	return 0;
}

int NetworkLayer::addLinksFromAll(int to, bool bidirectional, int latency)
{
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

int NetworkLayer::send(int fromProcess, int toProcess, Message const &msg) {
	return send(fromProcess, toProcess, msg.body);
}

int NetworkLayer::getLink(int p1, int p2)  const
{
	if (p1 < 0) return 0;
	if (p1 == p2) return 0;
	if (p1 >= (int)networkMap.size()) return -1;
	if (p2 >= (int)networkMap[p1].size()) return -1;
	return networkMap[p1][p2];
}

int NetworkLayer::send(int fromProcess, int toProcess, bytevector const &msg) {
	if (toProcess >= networkSize) {
		return ErrorSizeTooBig;
	}
	Message m(fromProcess, toProcess, msg);
	if (errorRate > 0 && randomNumberGenerator.nextDouble() < errorRate) {
		return ErrorTimeOut;
	}
	if (eventsMap[toProcess].wakeEvent == NULL) {
		return ErrorItemNotFound;
	}
	
	int p = getLink(fromProcess, toProcess);
	if (p < 0) {
		return ErrorItemNotFound;
	}
	m.sendTime = tick;
	m.deliveryTime = tick + p;
	eventsMap[toProcess].queue->enqueue(m);
	if (mode == ASYNC) {
		eventsMap[toProcess].wakeEvent->set();
	}
	return ErrorOK;
}

DS_THREAD_PROC NetworkLayer::globalTimerExecutor(void *arg )
{
	NetworkLayer *nl = (NetworkLayer *)arg;
	double start = rclock();
	while (!nl->stopFlag) {
		double cl = rclock() - start;
		nl->tick = (long long)cl;
		Thread::sleep(100);
	}
	return 0;
}

Thread::Thread () {
	_handle = NULL;
}

Thread::~Thread () {  
	kill();
}

Thread::ThreadHandle Thread::detach(Thread::ThreadProcPtr threadProc, void *parameter) {
	ThreadHandle handle;
#if DS_OS == DS_OSWINDOWS
	handle = ::CreateThread(NULL, 0, threadProc, parameter, 0, NULL);
	if (handle != INVALID_HANDLE_VALUE) {
		return handle;
	}
	return (ThreadHandle)NULL;
#else
	handle = new pthread_t();
	int err = pthread_create(handle, NULL, threadProc, parameter);
	if (err == 0) {
		return handle;
	} else {
		return (ThreadHandle)NULL;
	}
#endif
} 

void Thread::start(ThreadProcPtr threadProc, void *parameter)
{
	if (_handle == (ThreadHandle)NULL) {
#if DS_OS == DS_OSWINDOWS
		_handle = ::CreateThread(NULL, 0, threadProc, parameter, 0, NULL);
#else
		_handle = new pthread_t();
		int err = pthread_create(_handle, NULL, threadProc, parameter);
		err = err;
#endif
	}
} 

bool    Thread::wait(uint32 milliSeconds)
{
#if DS_OS == DS_OSWINDOWS
	if (_handle == (ThreadHandle)NULL || _handle == (ThreadHandle)INVALID_HANDLE_VALUE) {
		return false;
	}
	uint32 res = ::WaitForSingleObject(_handle, milliSeconds);
	if (res == WAIT_OBJECT_0) {
		return true;
	}
	return false;
#else
	int res = pthread_join(*_handle, NULL);
	if (res == 0)
		return true;
	return false;
#endif
} 

bool Thread::kill() // Вернуть true, если нить была и была убита
{
	if (_handle == (ThreadHandle)NULL || _handle == (ThreadHandle)INVALID_HANDLE_VALUE) {
		return false;
	}
#if DS_OS == DS_OSWINDOWS
	::TerminateThread(_handle, 0);
#else
	pthread_cancel(*_handle);
	delete _handle;
#endif
	_handle = (ThreadHandle)NULL;
	return true;
} 

void Thread::closeUnused(ThreadHandle handle)
{
#if DS_OS == DS_OSWINDOWS
	if (handle != NULL && handle != INVALID_HANDLE_VALUE)
		::CloseHandle(handle);
#endif
}

void Thread::sleep (uint32 milliSeconds)
{
#if DS_OS == DS_OSWINDOWS
	::Sleep(milliSeconds);
#else
	::usleep(milliSeconds * 1000);
#endif
} 

Process::Process(int _node) : workerEvent(false) {
	node = _node;
	stopFlag = false;
	workerThread.start(workerThreadExecutor, this);
}

Process::~Process() {
	stopFlag = true;
	workerThread.wait();
}

vector<int> Process::neibs() {
	return networkLayer->neibs(node);
}

int32 Process::registerWorkFunction(string const &prefix, workFunction wf) {
	workers.push_back(wf);
	return ErrorOK;
}

DS_THREAD_PROC Process::workerThreadExecutor(void *arg) {
	Process *dp = reinterpret_cast<Process *>(arg);
	while (!dp->stopFlag) {
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

bool Process::isMyMessage(string const &prefix, string const &message) {
	if (prefix.size()+1 >= message.size()) return false;
	for (size_t i = 0; i < prefix.size(); i++) {
		if (prefix[i] != message[i]) return false;
	}
	return message[prefix.size()] == '_';
}

World::~World() {
	for (size_t i = 0; i < processesList.size(); i++) {
		if (processesList[i] != NULL) {
			delete processesList[i];
			processesList[i] = NULL;
		}
	}
}

int World::createProcess(int node) {
	Process *p = new Process(node);
	if (node >= (int)processesList.size()) {
		processesList.resize(node+1);
	}
	processesList[node] = p;
	nl.registerProcess(node, p); 
	return node;
}

int World::assignWorkFunction(int node, string const &func) {
	if (node < 0 || node >= (int)processesList.size()) return ErrorItemNotFound;
	Process *dp = this->processesList[node];
	map<string, workFunction>::const_iterator it = associates.find(func);
	if (it == associates.end()) return ErrorItemNotFound;
	if (dp == NULL) return ErrorItemNotFound;
	dp->registerWorkFunction(func, it->second);
	return ErrorOK;
}

int World::registerWorkFunction(string const &func, workFunction wf) {
	associates.insert(make_pair(func, wf));
	return true;
}

bool World::parseConfig(string const &name) {
	ifstream f;
	f.open(name.c_str());
	if (!f) return false;
	char    s[1024];
	while (!f.eof()) {
		f.getline(s, sizeof s - 1);
		if (strlen(s) == 0 || s[0] == ';') continue;
		int startprocess, endprocess, from, to, latency = 1, arg;
		int unidirected = 1;
		char id[1024], msg[1024];
		if (sscanf(s, "unidirected %d\n", &unidirected) == 1) {
			continue;
		}
		else if (sscanf(s, "processes %d %d\n", &startprocess, &endprocess) == 2) {
			for (int i = startprocess; i <= endprocess; i++)	{
				createProcess(i);
			}
		} else if (sscanf(s, "link from %d to %d latency %d", &from, &to, &latency) == 3 || 
			sscanf(s, "link from %d to %d", &from, &to) == 2) { 
				nl.createLink(from, to, unidirected != 0, latency);
		} else if (sscanf(s, "link from %d to all latency %d", &from, &latency) == 2 ||
			sscanf(s, "link from %d to all", &from) == 1) {
				nl.addLinksToAll(from, unidirected != 0, latency);
		} else if (sscanf(s, "link from all to %d latency %d", &from, &latency) == 2 || 
			sscanf(s, "link from all to %d", &to) == 1) {
				nl.addLinksFromAll(from, unidirected != 0, latency);
		} else if (strcmp(s, "link from all to all") == 0 || 
			sscanf(s, "link from all to all latency %d", &latency) == 1) {
				nl.addLinksAllToAll(unidirected != 0, latency);
		} else if (sscanf(s, "setprocesses %d %d %s", &startprocess, &endprocess, id) == 3) {
			for (int i = startprocess; i <= endprocess; i++) {
				assignWorkFunction(i, id);
			}
		} else if (sscanf(s, "send from %d to %d %s %d", &from, &to, msg, &arg) == 4) {
			nl.send(from, to, Message(msg, arg));
		} else if (sscanf(s, "send from %d to %d %s", &from, &to, msg) == 3) {
			nl.send(from, to, Message(msg));
		} else if (strcmp(s, "wait all") == 0) {
			nl.waitForStill();
		} else {
			printf("unknown directive in input file: '%s'\n", s);
		}
	}
	f.close();
	return true;
}

