#pragma once

#include "../ChainState.h"

#include <TxHashSet.h>
#include <Config/Config.h>
#include <Hash.h>
#include <string>

// Forward Declarations
class IBlockChainServer;
class BlockHeader;
class IBlockDB;

class TxHashSetProcessor
{
public:
	TxHashSetProcessor(const Config& config, IBlockChainServer& blockChainServer, ChainState& chainState, IBlockDB& blockDB);

	ITxHashSet* ProcessTxHashSet(const Hash& blockHash, const std::string& path);

private:
	bool UpdateConfirmedChain(const BlockHeader& blockHeader);

	const Config& m_config;
	IBlockChainServer& m_blockChainServer;
	ChainState& m_chainState;
	IBlockDB& m_blockDB;
};