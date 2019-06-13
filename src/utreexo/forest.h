// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTREEXO_FOREST_H
#define BITCOIN_UTREEXO_FOREST_H
#include <primitives/block.h>
#include <sync.h>
#include <fs.h>
#include <streams.h>

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
    UtreexoForest(fs::path location);
    void Modify(const std::vector<uint256> adds, const std::vector<uint256> deletes);
    void Commit();
    void Empty();
    void PrintStats();
private:
    void addInternal(const std::vector<uint256> adds);
    UtreexoRootPhaseResult rootPhase(bool haveDel, bool haveRoot, uint64_t delPos, uint64_t rootPos, uint8_t h);
    UtreexoRootStash getSubTree(uint64_t src, bool del);
    void writeSubtree(UtreexoRootStash rootStash, uint64_t dest);
    void deleteInternal(const std::vector<uint256> dels);
    void moveSubtree(uint64_t from, uint64_t to);
    void reMap(uint8_t newHeight);
    void reHash();
    void resize(uint64_t newSize);
    void loadFromLocation(fs::path location);
    uint64_t size();
    uint256 getNode(uint64_t index);
    void setNode(uint64_t index, uint256 value);
    uint64_t numLeaves;
    uint8_t height;
    std::unique_ptr<CAutoFile> forest;
    FILE* forestFile;
    std::map<uint256, uint64_t> positionMap;
    std::map<uint64_t, bool> dirtyMap;
    CCriticalSection cs_forest;
    fs::path forestLocation;
};

#endif