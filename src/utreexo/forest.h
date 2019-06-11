// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTREEXO_FOREST_H
#define BITCOIN_UTREEXO_FOREST_H
#include <primitives/block.h>
#include <sync.h>
#include <fs.h>

struct UtreexoRootStash {
    std::vector<uint256> values;
    std::vector<uint64_t> dirties;
    std::vector<uint64_t> forgets;
};

struct UtreexoRootPhaseResult {
    uint64_t upDel;
    UtreexoRootStash rootStash;
};


class UtreexoForest {
public:
    UtreexoForest();
    void Modify(const std::vector<uint256> adds, const std::vector<uint256> deletes);
    void Commit(fs::path toDir, std::string filePrefix);
    void Load(fs::path fromDir, std::string filePrefix);
private:
    void addInternal(const std::vector<uint256> adds);
    UtreexoRootPhaseResult rootPhase(bool haveDel, bool haveRoot, uint64_t delPos, uint64_t rootPos, uint8_t h);
    UtreexoRootStash getSubTree(uint64_t src, bool del);
    void writeSubtree(UtreexoRootStash rootStash, uint64_t dest);
    void deleteInternal(const std::vector<uint256> dels);
    void moveSubtree(uint64_t from, uint64_t to);
    void reMap(uint8_t newHeight);
    void reHash();
    void printStats();
    uint64_t numLeaves;
    uint8_t height;
    std::vector<uint256> forest;
    std::map<uint256, uint64_t> positionMap;
    std::map<uint64_t, bool> dirtyMap;
    CCriticalSection cs_forest;
};

#endif