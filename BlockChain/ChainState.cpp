#include "ChainState.h"

#include <Consensus/BlockTime.h>
#include <Database/BlockDb.h>
#include <TxHashSet.h>

ChainState::ChainState(const Config& config, ChainStore& chainStore, BlockStore& blockStore, IHeaderMMR& headerMMR)
	: m_config(config), m_chainStore(chainStore), m_blockStore(blockStore), m_headerMMR(headerMMR)
{

}

ChainState::~ChainState()
{

}

void ChainState::Initialize(const BlockHeader& genesisHeader)
{
	Chain& candidateChain = m_chainStore.GetCandidateChain();
	const uint64_t candidateHeight = candidateChain.GetTip()->GetHeight();
	if (candidateHeight == 0)
	{
		m_blockStore.AddHeader(genesisHeader);
		m_headerMMR.AddHeader(genesisHeader);
	}
	else
	{
		const uint64_t horizon = std::max(candidateHeight, (uint64_t)Consensus::CUT_THROUGH_HORIZON) - (uint64_t)Consensus::CUT_THROUGH_HORIZON;

		std::vector<Hash> hashesToLoad;

		for (size_t i = horizon; i <= candidateHeight; i++)
		{
			const Hash& hash = candidateChain.GetByHeight(i)->GetHash();
			hashesToLoad.emplace_back(hash);
		}

		m_blockStore.LoadHeaders(hashesToLoad);
	}

	m_pTxHashSet = std::shared_ptr<ITxHashSet>(TxHashSetAPI::Open(m_config, m_blockStore.GetBlockDB()));
}

uint64_t ChainState::GetHeight(const EChainType chainType)
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	std::unique_ptr<BlockHeader> pHead = GetHead_Locked(chainType);
	if (pHead != nullptr)
	{
		return pHead->GetHeight();
	}

	return 0;
}

uint64_t ChainState::GetTotalDifficulty(const EChainType chainType)
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	std::unique_ptr<BlockHeader> pHead = GetHead_Locked(chainType);
	if (pHead != nullptr)
	{
		return pHead->GetProofOfWork().GetTotalDifficulty();
	}

	return 0;
}

std::unique_ptr<BlockHeader> ChainState::GetBlockHeaderByHash(const Hash& hash)
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	return m_blockStore.GetBlockHeaderByHash(hash);
}

std::unique_ptr<BlockHeader> ChainState::GetBlockHeaderByHeight(const uint64_t height, const EChainType chainType)
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	Chain& chain = m_chainStore.GetChain(chainType);
	const BlockIndex* pBlockIndex = chain.GetByHeight(height);
	if (pBlockIndex != nullptr)
	{
		return m_blockStore.GetBlockHeaderByHash(pBlockIndex->GetHash());
	}

	return std::unique_ptr<BlockHeader>(nullptr);
}

std::unique_ptr<BlockHeader> ChainState::GetHead_Locked(const EChainType chainType)
{
	const Hash& headHash = GetHeadHash_Locked(chainType);

	return m_blockStore.GetBlockHeaderByHash(headHash);
}

const Hash& ChainState::GetHeadHash_Locked(const EChainType chainType)
{
	return m_chainStore.GetChain(chainType).GetTip()->GetHash();
}

void ChainState::BlockValidated(const Hash& hash)
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	m_validatedBlocks.insert(hash);
}

bool ChainState::HasBlockBeenValidated(const Hash& hash) const
{
	std::shared_lock<std::shared_mutex> readLock(m_headersMutex);

	return m_validatedBlocks.find(hash) != m_validatedBlocks.cend();
}

LockedChainState ChainState::GetLocked()
{
	return LockedChainState(m_headersMutex, m_chainStore, m_blockStore, m_headerMMR, m_orphanPool, m_pTxHashSet);
}

void ChainState::FlushAll()
{
	std::unique_lock<std::shared_mutex> writeLock(m_headersMutex);

	m_headerMMR.Commit();
	m_chainStore.Flush();
	if (m_pTxHashSet != nullptr)
	{
		m_pTxHashSet->Commit();
	}
}