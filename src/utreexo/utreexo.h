// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTREEXO_UTREEXO_H
#define BITCOIN_UTREEXO_UTREEXO_H
#include <primitives/block.h>
#include <utreexo/forest.h>
#include <chain.h>
#define DEFAULT_UTREEXO_BRIDGE false
#define DEFAULT_UTREEXO_COMPACT false

class Utreexo {
public:
    Utreexo();
    void ProcessBlock(const CBlock& block);
    void ProveTx(const CTransaction& tx);
    void ProveBlock(const CBlock& block);
    void Reindex();
    void Commit();
    void Empty();
private:
    std::unique_ptr<UtreexoForest> forest;
};

Utreexo &GlobalUtreexo();
void InitUtreexo(bool fReindex);
bool UseUtreexo();
#endif