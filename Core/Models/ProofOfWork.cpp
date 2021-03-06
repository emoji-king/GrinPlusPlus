#include <Core/ProofOfWork.h>
#include <Consensus/BlockDifficulty.h>
#include <Crypto.h>

ProofOfWork::ProofOfWork(const uint64_t totalDifficulty, const uint32_t scalingDifficulty, const uint64_t nonce, const uint8_t edgeBits, std::vector<uint64_t>&& proofNonces)
	: m_totalDifficulty(totalDifficulty),
	m_scalingDifficulty(scalingDifficulty),
	m_nonce(nonce),
	m_edgeBits(edgeBits),
	m_proofNonces(std::move(proofNonces))
{

}

void ProofOfWork::Serialize(Serializer& serializer) const
{
	serializer.Append<uint64_t>(m_totalDifficulty);
	serializer.Append<uint32_t>(m_scalingDifficulty);
	serializer.Append<uint64_t>(m_nonce);
	serializer.Append<uint8_t>(m_edgeBits);
	SerializeProofNonces(serializer);
}

void ProofOfWork::SerializeProofNonces(Serializer& serializer) const
{
	const int bytes_len = ((m_edgeBits * Consensus::PROOFSIZE) + 7) / 8;
	std::vector<unsigned char> bits(bytes_len, 0);
	for (int n = 0; n < m_proofNonces.size(); n++)
	{
		for (int bit = 0; bit < m_edgeBits; bit++)
		{
			const uint64_t nonce = m_proofNonces[n];
			if ((nonce & ((uint64_t)1 << bit)) != 0)
			{
				const size_t positionTemp = (n * m_edgeBits) + bit;

				bits[positionTemp / 8] |= (uint8_t)(1 << (positionTemp % 8));
			}
		}
	}

	serializer.AppendByteVector(bits);
}

ProofOfWork ProofOfWork::Deserialize(ByteBuffer& byteBuffer)
{
	const uint64_t totalDifficulty = byteBuffer.ReadU64();
	const uint32_t scalingDifficulty = byteBuffer.ReadU32();
	const uint64_t nonce = byteBuffer.ReadU64();
	const uint8_t edgeBits = byteBuffer.ReadU8();
	std::vector<uint64_t> proofNonces = DeserializeProofNonces(byteBuffer, edgeBits);

	return ProofOfWork(totalDifficulty, scalingDifficulty, nonce, edgeBits, std::move(proofNonces));
}

std::vector<uint64_t> ProofOfWork::DeserializeProofNonces(ByteBuffer& byteBuffer, const uint8_t edgeBits)
{
	std::vector<uint64_t> proofNonces;
	const int bytes_len = ((edgeBits * Consensus::PROOFSIZE) + 7) / 8;

	const std::vector<unsigned char> bits = byteBuffer.ReadVector(bytes_len);
	for (int n = 0; n < Consensus::PROOFSIZE; n++)
	{
		uint64_t proofNonce = 0;
		for (int bit = 0; bit < edgeBits; bit++)
		{
			const size_t positionTemp = (n * edgeBits) + bit;

			if ((bits[positionTemp / 8] & (1 << ((uint8_t)positionTemp % 8))) != 0)
			{
				proofNonce |= ((uint64_t)1 << bit);
			}
		}

		proofNonces.push_back(proofNonce);
	}

	return proofNonces;
}

const CBigInteger<32>& ProofOfWork::GetHash() const
{
	if (m_hash == CBigInteger<32>())
	{
		Serializer serializer;
		SerializeProofNonces(serializer);
		m_hash = Crypto::Blake2b(serializer.GetBytes());
	}

	return m_hash;
}