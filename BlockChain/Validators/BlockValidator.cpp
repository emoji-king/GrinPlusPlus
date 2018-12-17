#include "BlockValidator.h"
#include "TransactionBodyValidator.h"
#include "../Processors/BlockHeaderProcessor.h"
#include "../CommitmentUtil.h"

#include <Crypto.h>
#include <Consensus/Common.h>

BlockValidator::BlockValidator(ChainState& chainState)
	: m_chainState(chainState)
{

}

// See: https://github.com/mimblewimble/docs/wiki/Validation-logic
bool BlockValidator::IsBlockValid(const FullBlock& block) const
{
	// 1. Check if already validated
	if (IsAlreadyValidated(block))
	{
		return false;
	}

	// 2. Validate header if it hasn't been validated already
	//if (m_chainState.GetBlockHeaderByHash(block.GetBlockHeader().Hash()) == nullptr)
	//{
	//	if (!BlockHeaderProcessor(m_chainState).ProcessSingleHeader(block.GetBlockHeader()))
	//	{
	//		return false;
	//	}
	//}

	// 3. Check if orphan
	//// Check if are processing the "next" block relative to the current chain head.
	//let head = ctx.batch.head() ? ;
	//if is_next_block(&b.header, &head) {
	//	// If this is the "next" block then either -
	//	//   * common case where we process blocks sequentially.
	//	//   * special case where this is the first fast sync full block
	//	// Either way we can proceed (and we know the block is new and unprocessed).
	//}
	//else {
	//	// At this point it looks like this is a new block that we have not yet processed.
	//	// Check we have the *previous* block in the store.
	//	// If we do not then treat this block as an orphan.
	//	check_prev_store(&b.header, &mut ctx.batch) ? ;
	//}

	std::unique_ptr<BlockHeader> pPreviousHeader = m_chainState.GetBlockHeaderByHash(block.GetBlockHeader().GetPreviousBlockHash());
	if (pPreviousHeader == nullptr)
	{
		// TODO: Treat as orphan.
		return false;
	}

	// 4. Validate that block is self-consistent
	if (!IsSelfConsistent(block, pPreviousHeader->GetTotalKernelOffset()))
	{
		return false;
	}

	// TODO: Finish this

	m_chainState.BlockValidated(block.GetBlockHeader().Hash());

	return true;
}

bool BlockValidator::IsAlreadyValidated(const FullBlock& block) const
{
	if (m_chainState.HasBlockBeenValidated(block.GetBlockHeader().Hash()))
	{
		return true;
	}

	return false;
}

// Validates all the elements in a block that can be checked without additional data. 
// Includes commitment sums and kernels, Merkle trees, reward, etc.
bool BlockValidator::IsSelfConsistent(const FullBlock& block, const BlindingFactor& previousKernelOffset) const
{
	if (!TransactionBodyValidator().ValidateTransactionBody(block.GetTransactionBody(), true))
	{
		return false;
	}

	if (!VerifyKernelLockHeights(block))
	{
		return false;
	}
		
	if (!VerifyCoinbase(block))
	{
		return false;
	}

	BlindingFactor blockKernelOffset(CBigInteger<32>::ValueOf(0));

	// take the kernel offset for this block (block offset minus previous) and verify.body.outputs and kernel sums
	if (block.GetBlockHeader().GetTotalKernelOffset() == previousKernelOffset)
	{
		blockKernelOffset = CommitmentUtil::AddKernelOffsets(std::vector<BlindingFactor>({ block.GetBlockHeader().GetTotalKernelOffset() }), std::vector<BlindingFactor>({ previousKernelOffset }));
	}

	return CommitmentUtil::VerifyKernelSums(block, 0 - Consensus::REWARD, blockKernelOffset);
}

// check we have no kernels with lock_heights greater than current height
// no tx can be included in a block earlier than its lock_height
bool BlockValidator::VerifyKernelLockHeights(const FullBlock& block) const
{
	const uint64_t height = block.GetBlockHeader().GetHeight();
	for (const TransactionKernel& kernel : block.GetTransactionBody().GetKernels())
	{
		if (kernel.GetLockHeight() > height)
		{
			return false;
		}
	}

	return true;
}

// Validate the coinbase outputs generated by miners.
// Check the sum of coinbase-marked outputs match the sum of coinbase-marked kernels accounting for fees.
bool BlockValidator::VerifyCoinbase(const FullBlock& block) const
{
	std::vector<Commitment> coinbaseCommitments;
	for (const TransactionOutput& output : block.GetTransactionBody().GetOutputs())
	{
		if ((output.GetFeatures() & EOutputFeatures::COINBASE_OUTPUT) == EOutputFeatures::COINBASE_OUTPUT)
		{
			coinbaseCommitments.push_back(output.GetCommitment());
		}
	}

	std::vector<Commitment> coinbaseKernelExcesses;
	for (const TransactionKernel& kernel : block.GetTransactionBody().GetKernels())
	{
		if ((kernel.GetFeatures() & EKernelFeatures::COINBASE_KERNEL) == EKernelFeatures::COINBASE_KERNEL)
		{
			coinbaseKernelExcesses.push_back(kernel.GetExcessCommitment());
		}
	}

	uint64_t reward = Consensus::REWARD;
	for (const TransactionKernel& kernel : block.GetTransactionBody().GetKernels())
	{
		reward += kernel.GetFee();
	}

	std::unique_ptr<Commitment> pRewardCommitment = Crypto::CommitTransparent(reward);
	if (pRewardCommitment == nullptr)
	{
		return false;
	}

	const std::vector<Commitment> overCommitment({ *pRewardCommitment });
	const std::unique_ptr<Commitment> pOutputAdjustedSum = Crypto::AddCommitments(coinbaseCommitments, overCommitment);

	const std::unique_ptr<Commitment> pKernelSum = Crypto::AddCommitments(coinbaseKernelExcesses, std::vector<Commitment>());

	// Verify the kernel sum equals the output sum accounting for block fees.
	if (pOutputAdjustedSum == nullptr || pKernelSum == nullptr)
	{
		return false;
	}

	return *pKernelSum == *pOutputAdjustedSum;
}