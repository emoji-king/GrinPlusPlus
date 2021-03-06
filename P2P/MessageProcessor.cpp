#include "MessageProcessor.h"
#include "MessageSender.h"
#include "Seed/PeerManager.h"
#include "BlockLocator.h"
#include "ConnectionManager.h"
#include "Common.h"

// Network Messages
#include "Messages/ErrorMessage.h"
#include "Messages/PingMessage.h"
#include "Messages/PongMessage.h"
#include "Messages/GetPeerAddressesMessage.h"
#include "Messages/PeerAddressesMessage.h"
#include "Messages/BanReasonMessage.h"

// BlockChain Messages
#include "Messages/GetHeadersMessage.h"
#include "Messages/HeaderMessage.h"
#include "Messages/HeadersMessage.h"
#include "Messages/BlockMessage.h"
#include "Messages/CompactBlockMessage.h"

// Transaction Message
#include "Messages/TransactionMessage.h"
#include "Messages/TxHashSetRequestMessage.h"
#include "Messages/TxHashSetArchiveMessage.h"

#include <HexUtil.h>
#include <StringUtil.h>
#include <FileUtil.h>
#include <BlockChainServer.h>
#include <Infrastructure/Logger.h>
#include <fstream>
#include <filesystem>

static const int BUFFER_SIZE = 64 * 1024;

using namespace MessageTypes;

MessageProcessor::MessageProcessor(const Config& config, ConnectionManager& connectionManager, PeerManager& peerManager, IBlockChainServer& blockChainServer)
	: m_config(config), m_connectionManager(connectionManager), m_peerManager(peerManager), m_blockChainServer(blockChainServer)
{

}

MessageProcessor::EStatus MessageProcessor::ProcessMessage(const uint64_t connectionId, ConnectedPeer& connectedPeer, const RawMessage& rawMessage)
{
	try
	{
		return ProcessMessageInternal(connectionId, connectedPeer, rawMessage);
	}
	catch (const DeserializationException&)
	{
		const EMessageType messageType = rawMessage.GetMessageHeader().GetMessageType();
		const std::string formattedIPAddress = connectedPeer.GetPeer().GetIPAddress().Format();
		LoggerAPI::LogError(StringUtil::Format("MessageProcessor::ProcessMessage - Deserialization exception while processing message(%d) from %s.", messageType, formattedIPAddress.c_str()));

		return EStatus::BAN_PEER;
	}
}

MessageProcessor::EStatus MessageProcessor::ProcessMessageInternal(const uint64_t connectionId, ConnectedPeer& connectedPeer, const RawMessage& rawMessage)
{
	const std::string formattedIPAddress = connectedPeer.GetPeer().GetIPAddress().Format();
	const MessageHeader& header = rawMessage.GetMessageHeader();
	if (header.IsValid())
	{
		switch (header.GetMessageType())
		{
			case Error:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const ErrorMessage errorMessage = ErrorMessage::Deserialize(byteBuffer);
				LoggerAPI::LogWarning("Error message retrieved from peer(" + formattedIPAddress + "): " + errorMessage.GetErrorMessage());

				return EStatus::BAN_PEER;
			}
			case BanReason:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const BanReasonMessage banReasonMessage = BanReasonMessage::Deserialize(byteBuffer);
				LoggerAPI::LogWarning("BanReason message retrieved from peer(" + formattedIPAddress + "): " + std::to_string(banReasonMessage.GetBanReason()));

				return EStatus::BAN_PEER;
			}
			case Ping:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const PingMessage pingMessage = PingMessage::Deserialize(byteBuffer);
				connectedPeer.UpdateTotals(pingMessage.GetTotalDifficulty(), pingMessage.GetHeight());

				const PongMessage pongMessage(m_blockChainServer.GetTotalDifficulty(EChainType::CONFIRMED), m_blockChainServer.GetHeight(EChainType::CONFIRMED));

				return MessageSender().Send(connectedPeer, pongMessage) ? EStatus::SUCCESS : EStatus::SOCKET_FAILURE;
			}
			case Pong:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const PongMessage pongMessage = PongMessage::Deserialize(byteBuffer);
				connectedPeer.UpdateTotals(pongMessage.GetTotalDifficulty(), pongMessage.GetHeight());

				return EStatus::SUCCESS;
			}
			case GetPeerAddrs:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const GetPeerAddressesMessage getPeerAddressesMessage = GetPeerAddressesMessage::Deserialize(byteBuffer);
				const Capabilities capabilities = getPeerAddressesMessage.GetCapabilities();

				const std::vector<Peer> peers = m_peerManager.GetPeers(capabilities.GetCapability(), P2P::MAX_PEER_ADDRS);
				std::vector<SocketAddress> socketAddresses;
				for (const Peer& peer : peers)
				{
					socketAddresses.push_back(peer.GetSocketAddress());
				}

				LoggerAPI::LogTrace(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Sending %lld addresses to %s.", socketAddresses.size(), formattedIPAddress.c_str()));
				const PeerAddressesMessage peerAddressesMessage(std::move(socketAddresses));

				return MessageSender().Send(connectedPeer, peerAddressesMessage) ? EStatus::SUCCESS : EStatus::SOCKET_FAILURE;
			}
			case PeerAddrs:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const PeerAddressesMessage peerAddressesMessage = PeerAddressesMessage::Deserialize(byteBuffer);
				const std::vector<SocketAddress>& peerAddresses = peerAddressesMessage.GetPeerAddresses();

				LoggerAPI::LogDebug(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Received %lld addresses from %s.", peerAddresses.size(), formattedIPAddress.c_str()));
				m_peerManager.AddPeerAddresses(peerAddresses);

				return EStatus::SUCCESS;
			}
			case GetHeaders:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const GetHeadersMessage getHeadersMessage = GetHeadersMessage::Deserialize(byteBuffer);
				const std::vector<CBigInteger<32>>& hashes = getHeadersMessage.GetHashes();

				std::vector<BlockHeader> blockHeaders = BlockLocator(m_blockChainServer).LocateHeaders(hashes);
				const HeadersMessage headersMessage(std::move(blockHeaders));

				LoggerAPI::LogDebug(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Sending %lld headers to %s.", blockHeaders.size(), formattedIPAddress.c_str()));
				return MessageSender().Send(connectedPeer, headersMessage) ? EStatus::SUCCESS : EStatus::SOCKET_FAILURE;
			}
			case Header:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const HeaderMessage headerMessage = HeaderMessage::Deserialize(byteBuffer);
				const BlockHeader& blockHeader = headerMessage.GetHeader();

				const EBlockChainStatus status = m_blockChainServer.AddBlockHeader(blockHeader);
				if (status == EBlockChainStatus::SUCCESS)
				{
					LoggerAPI::LogInfo(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Valid header %s received from %s.", blockHeader.FormatHash().c_str(), formattedIPAddress.c_str()));

					m_connectionManager.BroadcastMessage(headerMessage, connectionId);
				}
				else
				{
					LoggerAPI::LogDebug(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Header %s from %s not needed.", blockHeader.FormatHash().c_str(), formattedIPAddress.c_str()));
				}

				return (status == EBlockChainStatus::SUCCESS) ? EStatus::SUCCESS : EStatus::UNKNOWN_ERROR;
			}
			case Headers:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const HeadersMessage headersMessage = HeadersMessage::Deserialize(byteBuffer);
				const std::vector<BlockHeader>& blockHeaders = headersMessage.GetHeaders();

				LoggerAPI::LogDebug(StringUtil::Format("MessageProcessor::ProcessMessageInternal - %lld headers received from %s.", blockHeaders.size(), formattedIPAddress.c_str()));

				const bool added = m_blockChainServer.AddBlockHeaders(blockHeaders) == EBlockChainStatus::SUCCESS;
				LoggerAPI::LogInfo(StringUtil::Format("MessageProcessor::ProcessMessageInternal - Headers message from %s finished processing.", formattedIPAddress.c_str()));

				return added ? EStatus::SUCCESS : EStatus::UNKNOWN_ERROR;
			}
			case GetBlock:
			{
				return EStatus::SUCCESS;
			}
			case Block:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const BlockMessage blockMessage = BlockMessage::Deserialize(byteBuffer);
				const FullBlock& block = blockMessage.GetBlock();

				const EBlockChainStatus added = m_blockChainServer.AddBlock(block);
				if (added == EBlockChainStatus::SUCCESS)
				{
					m_connectionManager.BroadcastMessage(blockMessage, connectionId);
				}

				return EStatus::SUCCESS;
			}
			case GetCompactBlock:
			{
				return EStatus::SUCCESS;
			}
			case CompactBlockMsg:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const CompactBlockMessage compactBlockMessage = CompactBlockMessage::Deserialize(byteBuffer);
				const CompactBlock& compactBlock = compactBlockMessage.GetCompactBlock();

				const EBlockChainStatus added = m_blockChainServer.AddCompactBlock(compactBlock);
				if (added == EBlockChainStatus::SUCCESS)
				{
					m_connectionManager.BroadcastMessage(compactBlockMessage, connectionId);
				}

				return EStatus::SUCCESS;
			}
			case StemTransaction:
			{
				return EStatus::SUCCESS;
			}
			case TransactionMsg:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const TransactionMessage transactionMessage = TransactionMessage::Deserialize(byteBuffer);
				const Transaction& transaction = transactionMessage.GetTransaction();

				const EBlockChainStatus added = m_blockChainServer.AddTransaction(transaction);
				if (added == EBlockChainStatus::SUCCESS)
				{
					m_connectionManager.BroadcastMessage(transactionMessage, connectionId);
				}

				return EStatus::SUCCESS;
			}
			case TxHashSetRequest:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const TxHashSetRequestMessage txHashSetRequestMessage = TxHashSetRequestMessage::Deserialize(byteBuffer);

				return SendTxHashSet(connectionId, connectedPeer, txHashSetRequestMessage);
			}
			case TxHashSetArchive:
			{
				ByteBuffer byteBuffer(rawMessage.GetPayload());
				const TxHashSetArchiveMessage txHashSetArchiveMessage = TxHashSetArchiveMessage::Deserialize(byteBuffer);

				return ReceiveTxHashSet(connectionId, connectedPeer, txHashSetArchiveMessage);
			}
			default:
			{
				break;
			}
		}
	}

	return EStatus::SUCCESS;
}

MessageProcessor::EStatus MessageProcessor::SendTxHashSet(const uint64_t connectionId, ConnectedPeer& connectedPeer, const TxHashSetRequestMessage& txHashSetRequestMessage)
{
	LoggerAPI::LogInfo(StringUtil::Format("MessageProcessor::SendTxHashSet - Sending TxHashSet snapshot to %s.", connectedPeer.GetPeer().GetIPAddress().Format().c_str()));

	// TODO: Implmement
	return EStatus::SUCCESS;
}

MessageProcessor::EStatus MessageProcessor::ReceiveTxHashSet(const uint64_t connectionId, ConnectedPeer& connectedPeer, const TxHashSetArchiveMessage& txHashSetArchiveMessage)
{
	LoggerAPI::LogInfo(StringUtil::Format("MessageProcessor::ReceiveTxHashSet - Downloading TxHashSet from %s.", connectedPeer.GetPeer().GetIPAddress().Format().c_str()));

	const DWORD timeout = 25 * 1000;
	setsockopt(connectedPeer.GetConnection(), SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

	const std::string hashStr = HexUtil::ConvertHash(txHashSetArchiveMessage.GetBlockHash());
	const std::string txHashSetPath = m_config.GetTxHashSetDirectory() + StringUtil::Format("txhashset_%s.zip", hashStr.c_str());
	std::ofstream fout;
	fout.open(txHashSetPath, std::ios::binary | std::ios::out | std::ios::trunc);

	size_t bytesReceived = 0;
	std::vector<unsigned char> buffer(BUFFER_SIZE, 0);
	while (bytesReceived < txHashSetArchiveMessage.GetZippedSize())
	{
		const int expectedBytes = min((int)(txHashSetArchiveMessage.GetZippedSize() - bytesReceived), BUFFER_SIZE);
		const int newBytesReceived = recv(connectedPeer.GetConnection(), (char*)&buffer[0], expectedBytes, 0);
		if (newBytesReceived <= 0)
		{
			// Ban peer?
			LoggerAPI::LogError("MessageProcessor::ReceiveTxHashSet - Transmission ended abruptly.");
			fout.close();
			FileUtil::RemoveFile(txHashSetPath);

			return EStatus::BAN_PEER;
		}

		fout.write((char*)&buffer[0], newBytesReceived);
		bytesReceived += newBytesReceived;
	}

	fout.close();

	LoggerAPI::LogInfo("MessageProcessor::ReceiveTxHashSet - Downloading successful.");
	m_blockChainServer.ProcessTransactionHashSet(txHashSetArchiveMessage.GetBlockHash(), txHashSetPath);

	return EStatus::SUCCESS;
}