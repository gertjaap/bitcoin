// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTREEXO_UTIL_H
#define BITCOIN_UTREEXO_UTIL_H

#include <stdint.h>
#include <tuple>
#include <vector>

struct UtreexoTreeTops {
    std::vector<uint64_t> topIndices;
    std::vector<uint8_t> treeHeights;
};

struct UtreexoTwinData {
    std::vector<uint64_t> twins;
    std::vector<uint64_t> onlyChildren;
};

struct UtreexoMove {
    uint64_t from;
    uint64_t to;
};

class UtreexoUtil {
public:
    static uint8_t DetectHeight(uint64_t position, uint8_t forestHeight);
    static UtreexoTreeTops GetTops(uint64_t leaves, uint64_t forestHeight);
    static UtreexoTwinData ExtractTwins(std::vector<uint64_t> nodes);
    static uint64_t ChildMany(uint64_t position, uint8_t drop, uint8_t forestHeight);
    static std::vector<UtreexoMove> SubTreePositions(uint64_t subroot, uint64_t moveTo, uint8_t forestHeight);
    static std::vector<uint64_t> MergeSortedVectors(std::vector<uint64_t> a, std::vector<uint64_t> b);
    static uint64_t UpMany(uint64_t position, uint8_t rise, uint8_t forestHeight);
    static uint64_t Up1(uint64_t position, uint8_t forestHeight);
};
#endif