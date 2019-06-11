// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utreexo/util.h>
#include <logging.h>

uint8_t UtreexoUtil::DetectHeight(uint64_t position, uint8_t forestHeight) {
    uint64_t marker = (1 << forestHeight);
	uint8_t h;
	for(h = 0; (position&marker) != 0; h++) {
		marker >>= 1;
	}
	return h;
}

UtreexoTreeTops UtreexoUtil::GetTops(uint64_t leaves, uint64_t forestHeight) {
	UtreexoTreeTops tops;
	tops.topIndices = {};
	tops.treeHeights = {};

	uint64_t position = 0;

	// go left to right.  But append in reverse so that the tops are low to high
	// run though all bit positions.  if there's a 1, build a tree atop
	// the current position, and move to the right.
	for(uint8_t h = forestHeight; position < leaves; h--) {
		if(((1<<h)&leaves) != 0) {
			// build a tree here
			tops.topIndices.insert(tops.topIndices.begin(), UpMany(position, h, forestHeight));
			tops.treeHeights.insert(tops.treeHeights.begin(), h);
			position += (1<<h);
		}
	}
	
	
	return tops;
}

// go down drop times (always left; LSBs will be 0) and return position
uint64_t UtreexoUtil::ChildMany(uint64_t position, uint8_t drop, uint8_t forestHeight) {
	uint64_t mask = (2<<forestHeight) - 1;
	return (position << drop) & mask;
}

// Extracttwins takes a slice of ints and extracts the adjacent ints
// which differ only in the LSB.  It then returns two slices: one of the
// *even* twins (no odds), and one of the ints with no siblings
UtreexoTwinData UtreexoUtil::ExtractTwins(std::vector<uint64_t> nodes) {
	UtreexoTwinData twins;
	twins.onlyChildren = {};
	twins.twins = {};

	// run through the slice of deletions, and 'dedupe' by extracting siblings
	// (if both siblings are being deleted, nothing needs to move on that row)
	for(uint i = 0; i < nodes.size(); i++) {
		if (i+1 < nodes.size() && (nodes[i]|1) == (nodes[i+1])) {
			twins.twins.push_back(nodes[i]);
			++i; // skip one here
		} else {
			twins.onlyChildren.push_back(nodes[i]);
		}
	}

	return twins;
}

// subTreePositions takes in a node position and forestHeight and returns the
// positions of all children that need to move AND THE NODE ITSELF.  (it works nicer that way)
// Also it returns where they should move to, given the destination of the
// sub-tree root.
// can also be used with the "to" return discarded to just enumerate a subtree
// swap tells whether to activate the sibling swap to try to preserve order
std::vector<UtreexoMove> UtreexoUtil::SubTreePositions(uint64_t subroot, uint64_t moveTo, uint8_t forestHeight) {
	std::vector<UtreexoMove> moves = {};
	uint8_t subHeight = DetectHeight(subroot, forestHeight);
	int64_t rootDelta = (int64_t)moveTo - (int64_t)subroot;
	for(uint8_t height = 0; height <= subHeight; height++) {
		// find leftmost child at this height; also calculate the
		// delta (movement) for this row
		uint8_t depth = subHeight - height;
		uint64_t leftmost = ChildMany(subroot, depth, forestHeight);
		int64_t rowDelta = rootDelta << depth; // usually negative
		for(uint64_t i = 0; i < (uint64_t)(1<<depth); i++) {
			// loop left to right
			uint64_t f = leftmost + i;
			uint64_t t = (uint64_t)((int64_t)f + rowDelta);
			moves.push_back({f,t});
		}
	}

	return moves;
}


uint64_t UtreexoUtil::UpMany(uint64_t position, uint8_t rise, uint8_t forestHeight) {
	uint64_t mask = (2<<forestHeight) - 1;
	return ((position>>rise) | (mask << (forestHeight-(rise-1)))) & mask;
}

// MergeSortedVectors takes two vectors (of uint64_ts; though this seems
// genericizable in that it's just < and > operators) and merges them into
// a signle sorted vector, discarding duplicates.
// (eg [1, 5, 8, 9], [2, 3, 4, 5, 6] -> [1, 2, 3, 4, 5, 6, 8, 9]
std::vector<uint64_t> UtreexoUtil::MergeSortedVectors(std::vector<uint64_t> a, std::vector<uint64_t> b) {
	uint64_t maxA = a.size();
	uint64_t maxB = b.size();

	std::vector<uint64_t> result = {};
	// Make it too big, resize later
	result.resize(maxA+maxB);

	uint64_t idxA = 0;
	uint64_t idxB = 0;
	for(uint64_t i = 0; i < result.size(); i++) {	
		// if we're out of a or b, just use the remainder of the other one
		if(idxA >= maxA){
			std::copy(b.begin()+idxB, b.end(), result.begin()+i);
			i += (maxB-idxB);
			result.resize(i);
			break;
		}
		if(idxB >= maxB){
			std::copy(a.begin()+idxA, a.end(), result.begin()+i);
			i += (maxA-idxA);
			result.resize(i);
			break;
		}

		if(a[idxA] < b[idxB]) { // a is less so append that
			result[i] = a[idxA];
			++idxA;
		} else if (a[idxA] < b[idxB]) { // b is less so append that
			result[i] = b[idxB];
			idxB++;
		} else { // they're equal
			result[i] = a[idxA];
			++idxA;
			++idxB;
		}
	}

	return result;
}

// Return the position of the parent of this position
uint64_t UtreexoUtil::Up1(uint64_t position, uint8_t forestHeight) {
		return (position >> 1) | (1 << forestHeight);
}
