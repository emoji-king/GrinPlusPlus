#include "Connection.h"
#include "BaseMessageRetriever.h"
#include "MessageProcessor.h"
#include "MessageSender.h"
#include "ConnectionManager.h"
#include "Seed/PeerManager.h"

#include <Infrastructure/ThreadManager.h>
#include <thread>
#include <chrono>
#include <memory>

Connection::Connection(const uint64_t connectionId, const Config& config, ConnectionManager& connectionManager, PeerManager& peerManager, IBlockChainServer& blockChainServer, const ConnectedPeer& connectedPeer)
	: m_connectionId(connectionId), m_config(config), m_connectionManager(connectionManager), m_peerManager(peerManager), m_blockChainServer(blockChainServer), m_connectedPeer(connectedPeer)
{

}

bool Connection::Connect()
{
	if (IsConnectionActive())
	{
		return true;
	}

	m_terminate = true;
	if (m_connectionThread.joinable())
	{
		m_connectionThread.join();
	}

	m_terminate = false;

	m_connectionThread = std::thread(Thread_ProcessConnection, std::ref(*this));
	ThreadManagerAPI::SetThreadName(m_connectionThread.get_id(), "PEER_CONNECTION");

	return true;
}

bool Connection::IsConnectionActive() const
{
	if (m_terminate)
	{
		return false;
	}

	const time_t currentTime = std::time_t();
	const time_t lastContactTime = m_connectedPeer.GetPeer().GetLastContactTime();
	const double differenceInSeconds = std::difftime(currentTime, lastContactTime);
	
	return differenceInSeconds < 30.0;
}

void Connection::Disconnect()
{
	m_terminate = true;

	if (m_connectionThread.joinable())
	{
		m_connectionThread.join();
	}
}

void Connection::Send(const IMessage& message)
{
	std::lock_guard<std::mutex> lockGuard(m_sendMutex);
	m_sendQueue.emplace(message.Clone());
}

Peer Connection::GetPeer() const
{
	std::lock_guard<std::mutex> lockGuard(m_peerMutex);

	return m_connectedPeer.GetPeer();
}

uint64_t Connection::GetTotalDifficulty() const
{
	return m_connectedPeer.GetTotalDifficulty();
}

uint64_t Connection::GetHeight() const
{
	return m_connectedPeer.GetHeight();
}

Capabilities Connection::GetCapabilities() const
{
	return m_connectedPeer.GetPeer().GetCapabilities();
}

//
// Continuously checks for messages to send and/or receive until the connection is terminated.
// This function runs in its own thread.
//
void Connection::Thread_ProcessConnection(Connection& connection)
{
	MessageProcessor messageProcessor(connection.m_config, connection.m_connectionManager, connection.m_peerManager, connection.m_blockChainServer);
	const BaseMessageRetriever messageRetriever;

	while (!connection.m_terminate)
	{
		std::unique_lock<std::mutex> lockGuard(connection.m_peerMutex);

		// Check for received messages and if there is a new message, process it.
		std::unique_ptr<RawMessage> pRawMessage = messageRetriever.RetrieveMessage(connection.m_connectedPeer, BaseMessageRetriever::NON_BLOCKING);
		if (pRawMessage.get() != nullptr)
		{
			const MessageProcessor::EStatus status = messageProcessor.ProcessMessage(connection.m_connectionId, connection.m_connectedPeer, *pRawMessage);
		}

		// Send the next message in the queue, if one exists.
		std::unique_lock<std::mutex> sendLockGuard(connection.m_sendMutex);
		if (!connection.m_sendQueue.empty())
		{
			std::unique_ptr<IMessage> pMessageToSend(connection.m_sendQueue.front());
			connection.m_sendQueue.pop();

			MessageSender::Send(connection.m_connectedPeer, *pMessageToSend);
		}

		// TODO: If no message sent or received in last ~15 seconds, send ping.
		sendLockGuard.unlock();
		lockGuard.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}