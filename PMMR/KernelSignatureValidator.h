#pragma once

#include "KernelMMR.h"

class KernelSignatureValidator
{
public:
	bool ValidateKernelSignatures(const KernelMMR& kernelMMR) const;

private:
	bool ValidateKernelSignature(const TransactionKernel& kernel) const;
};