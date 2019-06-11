// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utreexo/forest.h>
#include <utreexo/util.h>
#include <clientversion.h>
#include <hash.h>
#include <random.h>
#include <tinyformat.h>
#include <logging.h>
#include <streams.h>
#include <fs.h>
#include <util/system.h>

UtreexoForest::UtreexoForest() : forest(1, uint256()) {
    positionMap = {};
    dirtyMap = {};
    height = 0;
    numLeaves = 0;
}

void UtreexoForest::Modify(const std::vector<uint256> adds, const std::vector<uint256> deletes) {
    int64_t delta = adds.size() - deletes.size();

    LOCK(cs_forest);
	// remap to expand the forest if needed
	while (numLeaves+delta > (uint64_t)(1<<height)) {
        reMap(height + 1);
	}

    deleteInternal(deletes);
    addInternal(adds);
    reHash();
	//printStats();
}

void UtreexoForest::printStats() {
    LogPrintf("Forest size: %d - Num Leaves: %d - Roots:\n", forest.size(), numLeaves);
    auto tops = UtreexoUtil::GetTops(numLeaves, height);
    for(uint i = 0; i < tops.topIndices.size(); i++) {
        LogPrintf("Tree [%d] - Height [%d] - Root [%s]\n", i, tops.treeHeights[i], forest[tops.topIndices[i]].GetHex());
    }

}

void UtreexoForest::Commit(fs::path toDir, std::string filePrefix) {
    LOCK(cs_forest);

    unsigned short randv = 0;
    GetRandBytes((unsigned char*)&randv, sizeof(randv));

    // open temp output file, and associate with CAutoFile
    fs::path pathTmp = toDir / strprintf("%s.%04x.tmp", filePrefix, randv);
    fs::path targetPath = toDir / strprintf("%s.dat", filePrefix);

    FILE *f = fsbridge::fopen(pathTmp.c_str(),"wb");
    CAutoFile fileout(f, SER_DISK, CLIENT_VERSION);
    for(uint i = 0; i < numLeaves; i++) {
        fileout << forest[i];
    }
    
    FileCommit(f);
    fileout.fclose();
    if (!RenameOver(pathTmp, targetPath))
        throw std::runtime_error(strprintf("%s: Rename-into-place failed", __func__));
}

void UtreexoForest::Load(fs::path fromDir, std::string filePrefix) {
    LOCK(cs_forest);

    fs::path fromPath = fromDir / strprintf("%s.dat", filePrefix);
    if(!fs::exists(fromPath)) return;

    LogPrintf("Loading Utreexo from %s\n", fromPath.string());

    FILE *f = fsbridge::fopen(fromPath.c_str(),"rb");
    CAutoFile filein(f, SER_DISK, CLIENT_VERSION);
    
    fseek(f,0,SEEK_END);

    LogPrintf("Utreexo is %d bytes\n", ftell(f));

    numLeaves = (ftell(f)/32);
    if(numLeaves == 0) {
        forest.resize(1);
        return;
    }
    forest.resize(numLeaves);

    LogPrintf("Num leaves is %d\n", numLeaves);

    fseek(f,0,SEEK_SET);
    
    for(uint i = 0; i < forest.size(); i++) {
        filein >> forest[i];
        dirtyMap[i] = true;
        positionMap[forest[i]] = i;
    }

    uint64_t neededForestSize = 1;
    while (numLeaves > (uint64_t)(1<<height)) {
        height++;
        neededForestSize += (1 << height);
        if(forest.size() < neededForestSize) {
            LogPrintf("Resizing forest to %d\n", neededForestSize);
            forest.resize(neededForestSize);
        }
	}

    filein.fclose();

    reHash();
    //printStats();
}

void UtreexoForest::reMap(uint8_t newHeight) {
    if(newHeight == height) {
        throw std::runtime_error(strprintf("%s: can't remap %d to %d, it's the same", std::string(__func__), height, newHeight));
    }

    if(abs(newHeight-height) > 1) {
        throw std::runtime_error(strprintf("%s: remap by more than 1 not supported (yet)", std::string(__func__)));
    }

    if(newHeight < height) {
        throw std::runtime_error(strprintf("%s: height reduction not implemented", std::string(__func__)));
    }
    forest.resize(forest.size() + (1 << newHeight));
    uint64_t pos = 1 << newHeight;  // Leftmost position of row 1
    uint64_t reach = pos >> 1;      // How much to next row up
    
    for(uint8_t h = 1; h < newHeight; h++) {
        uint64_t runLength = reach >> 1;
        for(uint64_t x = 0; x < runLength; x++) {
            if (forest.size() > (pos>>1)+x && !forest[(pos>>1)+x].IsNull()) {
                forest[pos+x] = forest[(pos>>1)+x];
            }
            if(dirtyMap[(pos>>1)+x]) {
                dirtyMap[pos+x] = true;
            }
        }
        pos += reach;
        reach >>= 1;
    }

    for(uint64_t x = 1 << height; x < (uint64_t)(1<<newHeight); x++) {   
		forest[x] = uint256();
		dirtyMap.erase(x);
	}
    
    height = newHeight;
}

void UtreexoForest::reHash() {
    if(height == 0) {
        return;
    }

    UtreexoTreeTops tops = UtreexoUtil::GetTops(numLeaves, height);

    std::vector<uint64_t> dirty = {};
    dirty.reserve(dirtyMap.size());
    for(auto it = dirtyMap.begin(); it != dirtyMap.end(); ++it) {
        dirty.push_back(it->first);
    }

    std::sort(dirty.begin(), dirty.end());
    std::vector<std::vector<uint64_t>> dirty2d = {};
    dirty2d.resize(height);
	
    uint8_t h = 0;
	uint64_t dirtyRemaining = 0;
	for(uint64_t pos : dirty){
		uint8_t dHeight = UtreexoUtil::DetectHeight(pos, height);
		if(h < dHeight) {
			h = dHeight;
		}
        dirty2d[h].push_back(pos);
		++dirtyRemaining;
	}

    std::vector<uint64_t> currentRow = {};
    std::vector<uint64_t> nextRow = {};

    for(h=0; h < height; h++) {
        currentRow = UtreexoUtil::MergeSortedVectors(currentRow, dirty2d[h]);
        dirtyRemaining -= dirty2d[h].size();
        if(dirtyRemaining == 0 && currentRow.size() == 0) {
            // done early
            break;
        }

        for(uint64_t i = 0; i < currentRow.size(); i++) {
            uint64_t pos = currentRow[i];
            if(i+1 < currentRow.size() && ((pos|1) == currentRow[i+1])) {
                // Don't hash siblings
                continue;
            }
            if(pos == tops.topIndices[0]) {
                // Don't hash tops
                continue;
            }

            uint64_t right = pos | 1;
            uint64_t left = right ^ 1;
            uint64_t parent = UtreexoUtil::Up1(left, height);
            CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
            ss << forest[left] << forest[right];
            forest[parent] = ss.GetHash();
            nextRow.push_back(parent);
        }
        if(tops.treeHeights[0] == h) {
            tops.topIndices.erase(tops.topIndices.begin());
            tops.treeHeights.erase(tops.treeHeights.begin());
        }
        currentRow = nextRow;
        nextRow = {};
    }

    dirtyMap = {};
}

void UtreexoForest::addInternal(const std::vector<uint256> adds) {
    for (uint256 add : adds) {
		forest[numLeaves] = add;
        positionMap[add] = numLeaves;
        dirtyMap[numLeaves] = true;
        numLeaves++;
	}
}

void UtreexoForest::deleteInternal(const std::vector<uint256> dels) {
    uint64_t numDeletions = dels.size();    
    std::vector<uint64_t> deletePositions = {};
    deletePositions.resize(numDeletions);
    for(uint i = 0; i < dels.size(); i++) {
        if(positionMap.find(dels[i]) == positionMap.end())
        {
            throw std::runtime_error(strprintf("%s: Tried to delete %s, but that's not in our tree", __func__, dels[i].GetHex()));
        }
        deletePositions[i] = positionMap[dels[i]];
    }

    // need a place to stash subtrees.  there's probably lots of ways to do a
	// better job of this.  pointers and stuff.  Have each height tree in a different
	// file or map, so that you don't have to move them twice.  Or keep subtrees
	// in serialized chunks.

	// the stash is a map of heights to stashes.  The stashes have slices
	// of hashes, from bottom left up to subroot.  Same ordering as the main forest
	// they also have dirty uint64s to indicate which hashes are dirty
    std::map<uint8_t, UtreexoRootStash> stashMap = {};

    // populate the map of root positions, and of root values
	// needed only to determine if you're deleting a root
    std::map<uint8_t, uint64_t> rootPosMap = {};
	std::map<uint8_t, uint64_t> nextRootPosMap = {};
	 UtreexoTreeTops tops = UtreexoUtil::GetTops(numLeaves, height);
    UtreexoTreeTops nextTops = UtreexoUtil::GetTops(numLeaves-dels.size(), height);
	for(uint i = 0; i < tops.topIndices.size(); i++) {
		rootPosMap[tops.treeHeights[i]] = tops.topIndices[i];
    }
	for(uint i = 0; i < nextTops.topIndices.size(); i++) {
		nextRootPosMap[nextTops.treeHeights[i]] = nextTops.topIndices[i];
    }
	
    std::vector<uint64_t> up1DeletePositions = {}; // The next deletions, one row up (in construction)
    
    /* all these steps need to happen for every floor, starting at sorting, and
	including extracting siblings.

	Steps for each floor:
	Sort (maybe not needed on upper floors?) (but can't hurt)
	Extract twins (move twins up 1 & delete, leave non-twins)
	Swap / condense remaining, and move children. <- flag dirty here
	If there are an odd number remaining, move to / from right root

	The Extract Twins step maybe could be left till the end?
	There's 2 different ways to do this, and it looks like it changes the
	order, so maybe try both ways and see which works best...
	A: first remove / promote twins, then swap OCs to compact.
	B: swap orphans into empty twins.  Doable!  May lead to empty twins
	more on the right side, which is .. good?  B seems "simpler" in a way
	in that you're not treating twins differently.

	Dirty bits for what to rehash are only set in the swap phase.  In extract, there's
	no need to hash anything as both siblings are gone.  In root phase, when something
	is derooted it's marked dirty, but not when something is rooted.
	It needs to be a dirty[map] because when you move subtrees, dirty positions also
	need to move.
	*/

	// the main floor loop.
	// per row:
	// sort / delete / extract / swap / root / promote
    for(uint8_t h = 0; h <= height; h++) {
		// *** skip.  if there are no deletions at this height, we're done
        if(deletePositions.size() == 0) {
			break;
		}

        // *** sort.  Probably pointless on upper floors..?
		std::sort(deletePositions.begin(), deletePositions.end());

		// *** delete
		// actually delete everything first (floor 0)
		// everywhere else you delete, there should probably be a ^1
		// except no, there's places where you move subtrees around & delete em
        for(uint64_t d : deletePositions) {
			forest[d] = uint256();
		}
		
        // check for root deletion (it can only be the last one)
		if(rootPosMap.find(h) != rootPosMap.end() && rootPosMap[h] == deletePositions[deletePositions.size()-1]) {
            deletePositions.erase(deletePositions.end()-1);
            rootPosMap.erase(h);
        }

        // *** extract / dedupe
		UtreexoTwinData twins = UtreexoUtil::ExtractTwins(deletePositions);
        deletePositions = twins.onlyChildren;

		for(uint64_t twin : twins.twins) {
			up1DeletePositions.push_back(UtreexoUtil::Up1(twin, height));
		}

		// *** swap
		while(deletePositions.size() > 1) {
			moveSubtree(deletePositions[1]^1, deletePositions[0]);
			dirtyMap[deletePositions[0]] = true;
            uint64_t up1Del = UtreexoUtil::Up1(deletePositions[1], height);
			up1DeletePositions.push_back(up1Del);
			deletePositions.erase(deletePositions.begin());
            deletePositions.erase(deletePositions.begin());
		}

        // *** root
		// the rightmost element of this floor *is* a root.
		// If we're deleting it, delete it now; its presence is important for
		// subsequent swaps
		// scenarios: deletion is present / absent, and root is present / absent

		// deletion, root: deroot
		// deletion, no root: rootify (possibly in place)
		// no deletion, root: stash root (it *will* collapse left later)
		// no deletion, no root: nothing to do


		// check if a root is present on this floor
		uint64_t rootPos = 0;
        bool rootPresent = false;
        if(rootPosMap.find(h) != rootPosMap.end()) {
            rootPos = rootPosMap[h];
            rootPresent = true;
        }
        // the last remaining deletion (if exists) can swap with the root

		// weve already deleted roots either in the delete phase, so there can't
		// be a root here that we are deleting. (though maybe make sure?)
		// so the 2 possibilities are: 1) root exists and root subtree moves to
		// fill the last deletion (derooting), or 2) root does not exist and last
		// OC subtree moves to root position (rooting)
		uint64_t deletePosition = 0;
        bool haveDeletion = false;
		if(deletePositions.size() == 1) {
			deletePosition = deletePositions[0];
			haveDeletion = true;
		}

        UtreexoRootPhaseResult rootResult = rootPhase(haveDeletion, rootPresent, deletePosition, rootPos, h);
		if(rootResult.upDel != 0) {
			// if de-rooting, interpret "updel" as a dirty position
			if(haveDeletion && rootPresent) {
				dirtyMap[rootResult.upDel] = true;
			} else {
				// otherwise it's an upDel
				up1DeletePositions.push_back(rootResult.upDel);
			}
		}
		if(rootResult.rootStash.values.size() != 0) {
			stashMap[h] = rootResult.rootStash;
		}

		// done with one row, set ds to the next slice
		deletePositions = up1DeletePositions;
		up1DeletePositions = {};
	}
	if(deletePositions.size() != 0) {
        throw std::runtime_error(strprintf("%s: finished deletion climb but %d deletion left", __func__, deletePositions.size()));
    }

	// move subtrees from the stash to where they should go
	for (auto it = stashMap.begin(); it != stashMap.end(); it++ )
    {
		uint64_t destPos = nextRootPosMap[it->first];
		writeSubtree(it->second, destPos);
	}

	// deletes have been applied, reduce numLeaves
	numLeaves -= numDeletions;
}

void UtreexoForest::writeSubtree(UtreexoRootStash rootStash, uint64_t dest) {
	uint8_t subheight = UtreexoUtil::DetectHeight(dest, height);
	if(rootStash.values.size() != ((2<<subheight)-1)) {
		throw std::runtime_error(strprintf("%s: height %d but %d nodes in arg subtree (need %d)", __func__, subheight, rootStash.values.size(), (2<<subheight)-1));
	}

    std::vector<UtreexoMove> moves = UtreexoUtil::SubTreePositions(dest, dest, height);
	// tos start at the bottom and move up, standard
    for(uint64_t i = 0; i < moves.size(); i++) {
		UtreexoMove m = moves[i];
        forest[m.to] = rootStash.values[i];
		if(i < (1 << subheight)) { // we're on the bottom row
			positionMap[rootStash.values[i]] = m.to;
		}
		if(rootStash.dirties.size() > 0 && rootStash.dirties[0] == i) {
			dirtyMap[m.to] = true;
			rootStash.dirties.erase(rootStash.dirties.begin());
		}
	}
}


// root phase is the most involved of the deletion phases.  broken out into its own
// method.  Returns a deletion and a stash.  If the deletion is 0 it's invalid
// as 0 can never be on a non-zero floor.
UtreexoRootPhaseResult UtreexoForest::rootPhase(bool haveDel, bool haveRoot, uint64_t delPos, uint64_t rootPos, uint8_t h) {
    UtreexoRootPhaseResult result;
    result.upDel = 0;

    // *** root
	// scenarios: deletion is present / absent, and root is present / absent

	// deletion, root: deroot, move to sibling
	// deletion, no root: rootify (possibly in place) & stash
	// no deletion, root: stash existing root (it *will* collapse left later)
	// no deletion, no root: nothing to do

	// weve already deleted roots either in the delete phase, so there can't
	// be a root here that we are deleting. (though maybe make sure?)

	if(haveDel && haveRoot) { // derooting.  simplest
		// root is present, move root to occupy the rightmost gap

		//		_, ok := f.forestMap[rootPos]
		if(forest[rootPos].IsNull()) {
			throw std::runtime_error(strprintf("%s: move from %d but empty", __func__, rootPos));
		}

		// move
		moveSubtree(rootPos, delPos);

		// delPos | 1 is to ensure it's not 0; marking either sibling dirty works
		// which is maybe weird and confusing...
		result.upDel = delPos | 1;
        return result;
	}

	if(!haveDel && !haveRoot) { // ok no that's even simpler
		return result;
	}

	uint64_t stashPos;

	// these are redundant, could just do if haveRoot / else here but helps to see
	// what's going on

	if(!haveDel && haveRoot) { // no deletion, root exists: stash it
		// if there are 0 deletions remaining we need to stash the
		// current root, because it will collapse leftward at the end as
		// deletions did occur on this floor.
		stashPos = rootPos;
	}

	if (haveDel && !haveRoot) { // rooting
		// if there's a deletion, the thing to stash is its sibling
		stashPos = delPos ^ 1;
		// mark parent for deletion. this happens even if the node
		// being promoted to root doesn't move
		result.upDel = UtreexoUtil::Up1(stashPos, height);
	}

	// .. even if the root is in the right place... it still needs to
	// be stashed.  Activity above it can overwrite it.

	// move either standing root or rightmost OC subtree to stash
	// but if this position is already in the right place, don't have to
	// stash it anywhere, so skip.  If skipped, a non-root node still
	// gets rooted, it just doesn't have to move.

	// read subtree.  In this case, also delete the subtree
	result.rootStash = getSubTree(stashPos, true);

	// stash the hashes in down to up order, and also stash the dirty bits
	// in any order (slice of uint64s)

	//	fmt.Printf("moved position %d to h %d root stash\n", stashPos, h)
	return result;
}

// getSubTree returns a subtree in []node format given a position in the forest
// deletes the subtree after reading it if del is true
UtreexoRootStash UtreexoForest::getSubTree(uint64_t src, bool del) {

	if(src >= forest.size() || forest[src].IsNull()) {
		throw std::runtime_error(strprintf("%s: subtree %d not in forest - out-of-bounds: %b - is null: %b", __func__, src, src >= forest.size(), forest[src].IsNull()));
	}

	UtreexoRootStash stash {{},{},{}};

	// get position listing of all nodes in subtree
	std::vector<UtreexoMove> moves = UtreexoUtil::SubTreePositions(src, src, height);

	stash.values.resize(moves.size());

	// read from map and build slice in down to up order
	for(int i = 0; i < moves.size(); i++) {
        UtreexoMove m = moves[i];
		stash.values[i] = forest[m.from];
		// node that the dirty positions are appended IN ORDER.
		// we can use that and don't have to sort through when we're
		// re-writing the dirtiness back
		if(dirtyMap[m.from]){
			stash.dirties.push_back(i);
			if(del) {
				dirtyMap.erase(m.from);
			}
		}

		if(del){
			forest[m.from] = uint256();
		}
	}

	return stash;
}


// moveSubtree moves a node, and all its children, from one place to another,
// and deletes everything at the prior location
// This is like get and write subtree but moving directly instead of stashing
void UtreexoForest::moveSubtree(uint64_t from, uint64_t to) {
	uint8_t fromHeight = UtreexoUtil::DetectHeight(from, height);
	uint8_t toHeight = UtreexoUtil::DetectHeight(to, height);
	if(fromHeight != toHeight) {
		throw std::runtime_error(strprintf("%s: Mismatched heights %d vs %d", __func__, fromHeight, toHeight));
	}

	std::vector<UtreexoMove> moves = UtreexoUtil::SubTreePositions(from, to, height);
	for(uint i = 0; i < moves.size(); i++) {
        UtreexoMove m = moves[i];

		if(forest[m.from].IsNull()) {
			throw std::runtime_error(strprintf("%s: move from %d but empty", __func__, from));
		}
		forest[m.to] = forest[m.from];

		if (i < (1 << toHeight)) { // we're on the bottom row
			positionMap[forest[m.to]] = m.to;
		}
		forest[m.from] = uint256();
		
		if(dirtyMap[m.from]) {
			dirtyMap[m.to] = true;
			dirtyMap.erase(m.from);
		}
    }
}