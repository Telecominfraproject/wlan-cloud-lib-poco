//
// SocketProactor.h
//
// Library: Net
// Package: Reactor
// Module:  SocketProactor
//
// Definition of the SocketProactor class.
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef Net_SocketProactor_INCLUDED
#define Net_SocketProactor_INCLUDED


#include "Poco/Net/Net.h"
#include "Poco/Net/Socket.h"
#include "Poco/Net/PollSet.h"
#include "Poco/Runnable.h"
#include "Poco/Timespan.h"
#include "Poco/Timestamp.h"
#include "Poco/AutoPtr.h"
#include "Poco/Mutex.h"
#include "Poco/Activity.h"
#include "Poco/NotificationQueue.h"
#include <unordered_map>
#include <atomic>
#include <functional>
#include <deque>
#include <utility>
#include <memory>


namespace Poco {


class Thread;


namespace Net {


class Socket;
class Worker;


class Net_API SocketProactor final: public Poco::Runnable
	/// This class implements the proactor pattern.
	/// It may also contain a simple work executor (enabled by default),
	/// which executes submitted workload.
{
public:
	using Buffer = std::vector<std::uint8_t>;
	using Work = std::function<void()>;
	using Callback = std::function<void (const std::error_code& failure, std::size_t bytesReceived)>;

	static const int POLL_READ = PollSet::POLL_READ;
	static const int POLL_WRITE = PollSet::POLL_WRITE;
	static const int POLL_ERROR = PollSet::POLL_ERROR;

	static const Timestamp::TimeDiff PERMANENT_COMPLETION_HANDLER;

	explicit SocketProactor(bool worker = true);
		/// Creates the SocketProactor.

	explicit SocketProactor(const Poco::Timespan& timeout, bool worker = true);
		/// Creates the SocketProactor, using the given timeout.

	SocketProactor(const SocketProactor&) = delete;
	SocketProactor(SocketProactor&&) = delete;
	SocketProactor& operator=(const SocketProactor&) = delete;
	SocketProactor& operator=(SocketProactor&&) = delete;

	~SocketProactor();
		/// Destroys the SocketProactor.

	void addWork(const Work& ch, Timestamp::TimeDiff ms = PERMANENT_COMPLETION_HANDLER);
		/// Adds work to be executed after the next poll() completion.
		/// Function will be called until the specified expiration,
		/// which defaults to immediately, ie. expiration after the
		/// first invocation.

	void addWork(Work&& ch, Timestamp::TimeDiff ms = PERMANENT_COMPLETION_HANDLER, int pos = -1);
		/// Adds work to be executed after the next poll() completion.
		/// Function will be called until the specified expiration,
		/// which defaults to immediately, ie. expiration after the
		/// first invocation.

	void removeWork();
		/// Removes all scheduled work.

	int scheduledWork();
		/// Returns the number of scheduled functions.

	int removeScheduledWork(int count = -1);
		/// Removes the count scheduled functions
		/// from the front of the schedule queue.
		/// Default is removal of all scheduled functions.

	int permanentWork();
		/// Returns the number of scheduled functions.

	int removePermanentWork(int count = -1);
		/// Removes the count permanent functions
		/// from the front of the schedule queue.
		/// Default is removal of all functions.

	int poll(int* pHandled = 0);
		/// Polls all registered sockets and calls their respective handlers.
		/// If pHandled is not null, after the call it contains the total number
		/// of read/write/error socket handlers called.
		/// Returns the number of completion handlers invoked.

	int runOne();
		/// Runs one handler, scheduled or permanent.
		/// If there are no available handlers, it blocks
		/// until the first handler is encountered and executed.
		/// Returns 1 on successful handler invocation, 0 on
		/// exception.

	void run();
		/// Runs the SocketProactor. The reactor will run
		/// until stop() is called (in a separate thread).

	void stop();
		/// Stops the SocketProactor.
		///
		/// The proactor will be stopped when the next event
		/// (including a timeout event) occurs.

	void wakeUp();
		/// Wakes up idle reactor.

	void setTimeout(const Poco::Timespan& timeout);
		/// Sets the timeout. 
		///
		/// If no other event occurs for the given timeout 
		/// interval, a timeout event is sent to all event listeners.
		///
		/// The default timeout is 250 milliseconds;
		///
		/// The timeout is passed to the Socket::select()
		/// method.

	Poco::Timespan getTimeout() const;
		/// Returns the timeout.

	void addSocket(Socket socket, int mode);
		/// Adds the socket to the poll set.

	void addReceiveFrom(Socket socket, Buffer& buf, SocketAddress& addr, Callback&& onCompletion);
		/// Adds the datagram socket and the completion handler to the I/O receive queue.

	void addSendTo(Socket socket, const Buffer& message, const SocketAddress& addr, Callback&& onCompletion);
		/// Adds the datagram socket and the completion handler to the I/O send queue.

	void addSendTo(Socket socket, const Buffer&& message, const SocketAddress&& addr, Callback&& onCompletion);
		/// Adds the datagram socket and the completion handler to the I/O send queue.

	void addReceive(Socket socket, Buffer& buf, Callback&& onCompletion);
		/// Adds the stream socket and the completion handler to the I/O receive queue.

	void addSend(Socket socket, const Buffer& message, Callback&& onCompletion);
		/// Adds the stream socket and the completion handler to the I/O send queue.

	void addSend(Socket socket, const Buffer&& message, Callback&& onCompletion);
		/// Adds the stream socket and the completion handler to the I/O send queue.

	bool has(const Socket& socket) const;
		/// Returns true if socket is registered with this proactor.

private:
	void onShutdown();
		/// Called when the SocketProactor is about to terminate.

	int doWork(bool handleOne = false, bool expiredOnly = false);
		/// Runs the scheduled work.
		/// If handleOne is true, only the next scheduled function
		/// is called.
		/// If expiredOnly is true, only expired temporary functions
		/// are called.

#ifdef POCO_HAVE_STD_ATOMICS
	typedef Poco::SpinlockMutex   MutexType;
	typedef MutexType::ScopedLock ScopedLock;
#else
	typedef Poco::FastMutex       MutexType;
	typedef MutexType::ScopedLock ScopedLock;
#endif // POCO_HAVE_STD_ATOMICS

	bool hasSocketHandlers();
	static const long DEFAULT_MAX_TIMEOUT_MS = 250;

	struct Handler
		/// Handler struct holds the scheduled I/O.
		/// At the actual I/O, Buffer and SocketAddress
		/// are used appropriately, and deleted if owned.
		/// Callback is passed to the IOCompletion queue.
	{
		Buffer* _pBuf = nullptr;
		SocketAddress* _pAddr = nullptr;
		Callback _onCompletion = nullptr;
		bool _owner = false;
	};

	class IONotification: public Notification
		/// IONotification object is used to transfer
		/// the I/O completion handlers into the
		/// completion handlers queue.
	{
	public:
		IONotification() = delete;

		IONotification(Callback&& onCompletion, int bytes, const std::error_code& errorCode):
			_onCompletion(std::move(onCompletion)),
			_bytes(bytes),
			_errorCode(errorCode)
			/// Creates the IONotification.
		{
		}

		~IONotification() = default;

		void call()
			/// Calls the completion handler.
		{
			_onCompletion(_errorCode, _bytes);
		};

	private:
		Callback _onCompletion;
		int _bytes;
		std::error_code _errorCode;
	};

	class IOCompletion
		/// IOCompletion utility class accompanies the
		/// SocketProactor and serves to execute I/O
		/// completion handlers in its own thread.
	{
	public:
		IOCompletion() = delete;

		explicit IOCompletion(int maxTimeout): _timeout(0),
			_maxTimeout(static_cast<long>(maxTimeout)),
			_activity(this, &IOCompletion::run),
			_pThread(nullptr)
			/// Creates IOCompletion.
		{
			_activity.start();
		}

		~IOCompletion() = default;

		void stop()
			/// Stops the I/O completion execution.
		{
			_activity.stop();
			_nq.wakeUpAll();
		}

		void wait()
			/// Blocks until I/O execution completely stops.
		{
			_activity.wait();
		}

		void enqueue(Notification::Ptr pNotification)
			/// Enqueues I/O completion.
		{
			_nq.enqueueNotification(std::move(pNotification));
		}

		void wakeUp()
			/// Wakes up the I/O completion execution loop.
		{
			if (_pThread) _pThread->wakeUp();
		}

	private:
		bool runOne()
			/// Runs the next I/O completion handler in the queue.
		{
			IONotification* pNf = dynamic_cast<IONotification*>(_nq.dequeueNotification());
			if (pNf)
			{
				pNf->call();
				pNf->release();
				return true;
			}
			return false;
		}

		void run()
			/// Continuously runs enqueued completion handlers.
		{
			_pThread = Thread::current();
			while(!_activity.isStopped())
			{
				SocketProactor::runImpl(!_nq.empty() && runOne(), _timeout, _maxTimeout);
			}
		}

		long _timeout;
		long _maxTimeout;
		Activity<IOCompletion> _activity;
		NotificationQueue _nq;
		Thread* _pThread;
	};

	using IOHandlerList = std::deque<std::unique_ptr<Handler>>;
	using IOHandlerIt = IOHandlerList::iterator;
	using SubscriberMap = std::unordered_map<poco_socket_t, std::deque<std::unique_ptr<Handler>>>;

	static void runImpl(bool runCond, long &sleepMS, long maxSleep);

	int send(Socket& socket);
		/// Calls the appropriate output function; enqueues
		/// the accompanying completion handler and removes
		/// it from the handlers list after the operation
		/// successfully completes.

	int receive(Socket& socket);
		/// Calls the appropriate input function; enqueues
		/// the accompanying completion handler and removes
		/// it from the handlers list after the operation
		/// successfully completes.

	void addSend(Socket socket, Buffer* pMessage, SocketAddress* pAddr, Callback&& onCompletion, bool own = false);
		/// Adds the datagram socket and the completion handler to the I/O send queue.
		/// If `own` is true, message and address are deleted after the I/O completion.

	void sendTo(SocketImpl& socket, IOHandlerIt& it);
		/// Sends data to the datagram socket and enqueues the
		/// accompanying completion handler.

	void send(SocketImpl& socket, IOHandlerIt& it);
		/// Sends data to the stream socket and enqueues the
		/// accompanying completion handler.

	void receiveFrom(SocketImpl& socket, IOHandlerIt& it, int available);
		/// Reads data from the datagram socket and enqueues the
		/// accompanying completion handler.

	void receive(SocketImpl& socket, IOHandlerIt& it, int available);
		/// Reads data from the stream socket and enqueues the
		/// accompanying completion handler.

	void enqueueIONotification(Callback&& onCompletion, int n, int err);
		/// Enqueues the completion handler into the I/O
		/// completion handler.

	Worker& worker();

	std::atomic<bool> _stop;
	long              _timeout;
	long              _maxTimeout;
	PollSet           _pollSet;
	Poco::Thread*     _pThread;

	SubscriberMap _readHandlers;
	SubscriberMap _writeHandlers;
	IOCompletion  _ioCompletion;
	Poco::Mutex   _writeMutex;
	Poco::Mutex   _readMutex;

	std::unique_ptr<Worker> _pWorker;
	friend class Worker;
};

//
// inlines
//

inline void SocketProactor::enqueueIONotification(Callback&& onCompletion, int n, int err)
{
	if (onCompletion)
	{
		_ioCompletion.enqueue(new IONotification(
				std::move(onCompletion), n,
				std::error_code(err, std::generic_category())));
	}
}


} } // namespace Poco::Net


#endif // Net_SocketProactor_INCLUDED
