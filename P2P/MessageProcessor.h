#pragma once

#include "ConnectedPeer.h"
#include "Messages/RawMessage.h"

#include <Config/Config.h>

// Forward Declarations
class ConnectionManager;
class IBlockChainServer;
class ConnectedPeer;
class RawMessage;
class TxHashSetArchiveMessage;
class TxHashSetRequestMessage;
class PeerManager;

class MessageProcessor
{
public:
	enum EStatus
	{
		SUCCESS,
		SOCKET_FAILURE,
		UNKNOWN_ERROR,
		BAN_PEER
	};

	MessageProcessor(const Config& config, ConnectionManager& connectionManager, PeerManager& peerManager, IBlockChainServer& blockChainServer);

	EStatus ProcessMessage(const uint64_t connectionId, ConnectedPeer& connectedPeer, const RawMessage& rawMessage);

private:
	EStatus ProcessMessageInternal(const uint64_t connectionId, ConnectedPeer& connectedPeer, const RawMessage& rawMessage);
	EStatus SendTxHashSet(const uint64_t connectionId, ConnectedPeer& connectedPeer, const TxHashSetRequestMessage& txHashSetRequestMessage);
	EStatus ReceiveTxHashSet(const uint64_t connectionId, ConnectedPeer& connectedPeer, const TxHashSetArchiveMessage& txHashSetArchiveMessage);

	const Config& m_config;
	ConnectionManager& m_connectionManager;
	PeerManager& m_peerManager;
	IBlockChainServer& m_blockChainServer;
};