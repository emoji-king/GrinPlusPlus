#pragma once

#include "TxHashSetValidationResult.h"

#include <Core/BlockHeader.h>

// Forward Declarations
class HashFile;
class TxHashSet;
class KernelMMR;
class IBlockChainServer;
class MMR;
class Commitment;

class TxHashSetValidator
{
public:
	TxHashSetValidator(const IBlockChainServer& blockChainServer);

	TxHashSetValidationResult Validate(TxHashSet& txHashSet, const BlockHeader& blockHeader, Commitment& outputSumOut, Commitment& kernelSumOut) const;

private:
	bool ValidateSizes(TxHashSet& txHashSet, const BlockHeader& blockHeader) const;
	bool ValidateMMRHashes(const MMR& mmr) const;
	bool ValidateRoots(TxHashSet& txHashSet, const BlockHeader& blockHeader) const;

	bool ValidateKernelHistory(const KernelMMR& kernelMMR, const BlockHeader& blockHeader) const;
	bool ValidateRangeProofs(TxHashSet& txHashSet, const BlockHeader& blockHeader) const;
	bool ValidateKernelSignatures(TxHashSet& txHashSet, const BlockHeader& blockHeader) const;

	const IBlockChainServer& m_blockChainServer;
};