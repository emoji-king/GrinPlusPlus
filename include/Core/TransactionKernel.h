#pragma once

//
// This code is free for all purposes without any express guarantee it works.
//
// Author: David Burkett (davidburkett38@gmail.com)
//

#include <Core/Features.h>
#include <Crypto/Commitment.h>
#include <Crypto/Signature.h>
#include <Serialization/ByteBuffer.h>
#include <Serialization/Serializer.h>

////////////////////////////////////////
// TRANSACTION KERNEL
////////////////////////////////////////
class TransactionKernel
{
public:
	//
	// Constructors
	//
	TransactionKernel(const EKernelFeatures features, const uint64_t fee, const uint64_t lockHeight, Commitment&& excessCommitment, Signature&& excessSignature);
	TransactionKernel(const TransactionKernel& transactionKernel) = default;
	TransactionKernel(TransactionKernel&& transactionKernel) noexcept = default;
	TransactionKernel() = default;

	//
	// Destructor
	//
	~TransactionKernel() = default;

	//
	// Operators
	//
	TransactionKernel& operator=(const TransactionKernel& transactionKernel) = default;
	TransactionKernel& operator=(TransactionKernel&& transactionKernel) noexcept = default;
	inline bool operator<(const TransactionKernel& transactionKernel) const { return Hash() < transactionKernel.Hash(); }
	inline bool operator==(const TransactionKernel& transactionKernel) const { return Hash() == transactionKernel.Hash(); }
	inline bool operator!=(const TransactionKernel& transactionKernel) const { return Hash() != transactionKernel.Hash(); }

	//
	// Getters
	//
	inline const EKernelFeatures GetFeatures() const { return m_features; }
	inline const uint64_t GetFee() const { return m_fee; }
	inline const uint64_t GetLockHeight() const { return m_lockHeight; }
	inline const Commitment& GetExcessCommitment() const { return m_excessCommitment; }
	inline const Signature& GetExcessSignature() const { return m_excessSignature; }

	//
	// Serialization/Deserialization
	//
	void Serialize(Serializer& serializer) const;
	static TransactionKernel Deserialize(ByteBuffer& byteBuffer);

	//
	// Hashing
	//
	const CBigInteger<32>& Hash() const;

private:
	// Options for a kernel's structure or use
	EKernelFeatures m_features;

	// Fee originally included in the transaction this proof is for.
	uint64_t m_fee;

	// This kernel is not valid earlier than m_lockHeight blocks
	// The max m_lockHeight of all *inputs* to this transaction
	uint64_t m_lockHeight;

	// Remainder of the sum of all transaction commitments. 
	// If the transaction is well formed, amounts components should sum to zero and the excess is hence a valid public key.
	Commitment m_excessCommitment;

	// The signature proving the excess is a valid public key, which signs the transaction fee.
	Signature m_excessSignature;

	mutable CBigInteger<32> m_hash;
};