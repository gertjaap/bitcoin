// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utreexo/utreexo.h>
#include <interfaces/chain.h>
#include <assert.h>
#include <memory>
#include <logging.h>
#include <util/system.h>
#include <hash.h>
#include <iomanip>
#include <sstream>

Utreexo::Utreexo() {
    forest = std::unique_ptr<UtreexoForest>(new UtreexoForest(GetDataDir() / "utreexo.dat"));
}

void Utreexo::ProcessBlock(const CBlock& block) {
    std::vector<uint256> adds = {};
    std::vector<uint256> dels = {};
  
    for(const CTransactionRef& tx : block.vtx) {
        if(!tx->IsCoinBase()) {
            for(const CTxIn& in : tx->vin) {
                dels.push_back(SerializeHash(in.prevout));
            }
        }
        for(uint32_t i = 0; i < tx->vout.size(); i++) {
            adds.push_back(SerializeHash(COutPoint(tx->GetHash(), i)));
        }
    }

    for(int a = (adds.size()-1); a >= 0; a--) {
        for(int d = (dels.size()-1); d >= 0; d--) {
            if(adds[a] == dels[d]) {
                adds.erase(adds.begin()+a);
                dels.erase(dels.begin()+d);
                break;
            }
        }
    }
    forest->Modify(adds, dels);
}

void Utreexo::Reindex() {
    std::unique_ptr<interfaces::Chain> chain = interfaces::MakeChain(); 
    
    uint256 block_hash = uint256();
    double progress = 0;    
    Optional<int> block_height;
    {
        auto locked_chain = chain->lock();
        block_hash = locked_chain->getBlockHash(0);
        block_height = locked_chain->getBlockHeight(block_hash);
    }
    int64_t nNow = GetSystemTimeInSeconds();

    while (block_height && !chain->shutdownRequested()) {
        if (GetSystemTimeInSeconds() >= nNow + 5) {
            nNow = GetSystemTimeInSeconds();
            LogPrintf("Rebuilding Utreexo. At block %d. Progress=%f\n", *block_height, progress);
            forest->PrintStats();
        }

        CBlock block;
        if (chain->findBlock(block_hash, &block) && !block.IsNull()) {
            auto locked_chain = chain->lock();
            if (!locked_chain->getBlockHeight(block_hash)) {
                break;
            }
            ProcessBlock(block);
        }
        {
            auto locked_chain = chain->lock();
            Optional<int> tip_height = locked_chain->getHeight();
            if (!tip_height || *tip_height <= block_height || !locked_chain->getBlockHeight(block_hash)) {
                // break successfully when rescan has reached the tip, or
                // previous block is no longer on the chain due to a reorg
                break;
            }

            // increment block and verification progress
            block_hash = locked_chain->getBlockHash(++*block_height);
            progress = chain->guessVerificationProgress(block_hash);
        }
    }

    LogPrintf("Rebuilt Utreexo:\n");
    forest->PrintStats();
}

void Utreexo::Commit() {
    LogPrintf("Committing Utreexo\n");
    forest->Commit();
}

void Utreexo::Empty() {
    forest->Empty();
}

static std::unique_ptr<Utreexo> m_global_utreexo;
static int m_use_utreexo = -1;

Utreexo &GlobalUtreexo() {
    assert(m_global_utreexo);
    return *m_global_utreexo;
}

void InitUtreexo(bool fReindex)
{
    if(UseUtreexo()) {
        m_global_utreexo = std::unique_ptr<Utreexo>(new Utreexo());
        if(fReindex) {
            m_global_utreexo->Empty();
        }
    }
}

bool UseUtreexo()
{
    if(m_use_utreexo == -1) {
        if(gArgs.GetBoolArg("-utreexobridge", DEFAULT_UTREEXO_BRIDGE) || gArgs.GetBoolArg("-utreexocompact", DEFAULT_UTREEXO_COMPACT)) {
            m_use_utreexo = 1;
        } else {
            m_use_utreexo = 0;
        }
    }
    return m_use_utreexo == 1;
}