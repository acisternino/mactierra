/*
 *  MT_World.cpp
 *  MacTierra
 *
 *  Created by Simon Fraser on 8/10/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#define __STDC_LIMIT_MACROS
#include <stdint.h>

#include <map>

#include <sstream>

#include "MT_World.h"

#include "RandomLib/ExponentialDistribution.hpp"

#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#include <boost/serialization/serialization.hpp>

#include "MT_Cellmap.h"
#include "MT_Creature.h"
#include "MT_ExecutionUnit0.h"
#include "MT_Genotype.h"
#include "MT_InstructionSet.h"
#include "MT_Inventory.h"
#include "MT_Soup.h"

namespace MacTierra {

using namespace std;

World::World()
: mRNG(0)
, mSoupSize(0)
, mSoup(NULL)
, mCellMap(NULL)
, mNextCreatureID(1)
, mExecution(NULL)
, mTimeSlicer(this)
, mInventory(NULL)
, mCurCreatureCycles(0)
, mCurCreatureSliceCycles(0)
, mCopyErrorPending(false)
, mCopiesSinceLastError(0)
, mNextCopyError(0)
, mNextFlawInstruction(0)
, mNextCosmicRayInstruction(0)
{
}

World::~World()
{
    destroyCreatures();
    delete mSoup;
    delete mCellMap;
    delete mExecution;
    delete mInventory;
}

void
World::initializeSoup(u_int32_t inSoupSize)
{
    BOOST_ASSERT(!mSoup && !mCellMap);
    mSoupSize = inSoupSize;
    
    mSettings.updateWithSoupSize(mSoupSize);
    
    mSoup = new Soup(inSoupSize);
    mCellMap = new CellMap(inSoupSize);

    mExecution = new ExecutionUnit0();
    
    mInventory = new Inventory();
    
    // FIXME get real number
    mTimeSlicer.setDefaultSliceSize(20);
}

Creature*
World::createCreature()
{
    if (!mSoup)
        return NULL;

    Creature*   theCreature = new Creature(uniqueCreatureID(), mSoup);
    
    return theCreature;
}

void
World::eradicateCreature(Creature* inCreature)
{
    if (inCreature->isDividing())
    {
        Creature* daughterCreature = inCreature->daughterCreature();
        BOOST_ASSERT(daughterCreature);
        
        if (mSettings.clearReapedCreatures())
            daughterCreature->clearSpace();

        mCellMap->removeCreature(daughterCreature);
        
        inCreature->clearDaughter();
        
        // daughter should not be in reaper or slicer yet.
        delete daughterCreature;
    }
    
    //mReaper.printCreatures();
    
    if (mSettings.clearReapedCreatures())
        inCreature->clearSpace();

    // remove from cell map
    mCellMap->removeCreature(inCreature);
    
    // remove from slicer and reaper
    creatureRemoved(inCreature);
    
    delete inCreature;
}

Creature*
World::insertCreature(address_t inAddress, const instruction_t* inInstructions, u_int32_t inLength)
{
    if (!mCellMap->spaceAtAddress(inAddress, inLength))
        return NULL;

    Creature* theCreature = createCreature();

    theCreature->setLocation(inAddress);
    theCreature->setLength(inLength);
    
    mSoup->injectInstructions(inAddress, inInstructions, inLength);

    InventoryGenotype* theGenotype = NULL;
    bool isNew = mInventory->enterGenotype(theCreature->genomeData(), theGenotype);
    if (isNew)
    {
        theGenotype->setOriginInstructions(mTimeSlicer.instructionsExecuted());
        theGenotype->setOriginGenerations(1);
    }
    BOOST_ASSERT(theGenotype);
    theCreature->setGenotype(theGenotype);
    theCreature->setGeneration(1);
    theGenotype->creatureBorn();

    theCreature->setSliceSize(mTimeSlicer.initialSliceSizeForCreature(theCreature, mSettings.sizeSelection()));
    theCreature->setReferencedLocation(theCreature->location());
    
    bool inserted = mCellMap->insertCreature(theCreature);
    BOOST_ASSERT(inserted);

    // add it to the various queues
    creatureAdded(theCreature);
    
    theCreature->onBirth(*this);     // IVF, kinda
    return theCreature;
}

void
World::iterate(u_int32_t inNumCycles)
{
    u_int32_t   cycles = 0;
    bool        tracing = false;
    u_int32_t   numCycles = tracing ? 1 : inNumCycles;      // unless tracing
    
    Creature*   curCreature = mTimeSlicer.currentCreature();
    if (!curCreature)
        return;

    if (mCurCreatureCycles == 0)
        mCurCreatureSliceCycles = mTimeSlicer.sizeForThisSlice(curCreature, mSettings.sliceSizeVariance());
    
    while (cycles < numCycles)
    {
        if (mCurCreatureCycles < mCurCreatureSliceCycles)
        {
            const u_int64_t instructionCount = mTimeSlicer.instructionsExecuted();

            // do cosmic rays
            if (timeForCosmicRay(instructionCount))
                cosmicRay(instructionCount);
            
            // decide whether to throw in a flaw
            int32_t flaw = 0;
            if (timeForFlaw(instructionCount))
                flaw = instructionFlaw(instructionCount);
            
            // TODO: track leanness

            // execute the next instruction
            Creature* daughterCreature = mExecution->execute(*curCreature, *this, flaw);
            if (daughterCreature)
                handleBirth(curCreature, daughterCreature);
        
            // if there was an error, adjust in the reaper queue
            if (curCreature->cpu().flag())
                mReaper.conditionalMoveUp(*curCreature);
            else if (curCreature->lastInstruction() == k_mal || curCreature->lastInstruction() == k_divide)
                mReaper.conditionalMoveDown(*curCreature);

            // compute next copy error time
            if (mSettings.copyErrorRate() > 0.0 & (curCreature->lastInstruction() == k_mov_iab))
                noteInstructionCopy();
            
            ++mCurCreatureCycles;
            mTimeSlicer.executedInstruction();

            ++cycles;
        }
        else        // we are at the end of the slice for one creature
        {
            // maybe reap
            if (mCellMap->fullness() > mSettings.reapThreshold())
            {
                //mReaper.printCreatures();
                Creature* doomedCreature = mReaper.headCreature();
                //cout << "Reaping creature " << doomedCreature->creatureID() << " (" << doomedCreature->numErrors() << " errors)" << endl;
                handleDeath(doomedCreature);
            }
            
            // maybe kill off long-lived creatures
            
            
            // rotate the slicer
            bool cycled = mTimeSlicer.advance();
            if (cycled)
            {
                //mInventory->printCreatures();
            }
            
            // start on the next creature
            curCreature = mTimeSlicer.currentCreature();
            if (!curCreature)
                break;

            // cout << "Running creature " << curCreature->creatureID() << endl;
            
            // track the new creature for tracing
            
            mCurCreatureCycles = 0;
            mCurCreatureSliceCycles = mTimeSlicer.sizeForThisSlice(curCreature, mSettings.sliceSizeVariance());
        }
    }
    
    //cout << "Executed " << mTimeSlicer.instructionsExecuted() << " instructions" << endl;
}

instruction_t
World::mutateInstruction(instruction_t inInst, Settings::EMutationType inMutationType) const
{
    instruction_t resultInst = inInst;

    switch (inMutationType)
    {
        case Settings::kAddOrDec:
            {
                int32_t delta = mRNG.Boolean() ? -1 : 1;
                resultInst = (inInst + kInstructionSetSize + delta) % kInstructionSetSize;
            }
            break;

        case Settings::kBitFlip:
            resultInst ^= (1 << mRNG.Integer(5));
            break;

        case Settings::kRandomChoice:
            resultInst = mRNG.Integer(kInstructionSetSize);
            break;
    }
    return resultInst;
}

#pragma mark -

creature_id
World::uniqueCreatureID()
{
    creature_id nextID = mNextCreatureID;
    ++mNextCreatureID;
    return nextID;
}

void
World::destroyCreatures()
{
    CreatureIDMap::const_iterator theEnd = mCreatureIDMap.end();
    for (CreatureIDMap::const_iterator it = mCreatureIDMap.begin(); it != theEnd; ++it)
    {
        Creature* theCreature = (*it).second;
        
        mCellMap->removeCreature(theCreature);

        mTimeSlicer.removeCreature(*theCreature);
        mReaper.removeCreature(*theCreature);

        theCreature->clearDaughter();
        delete theCreature;
    }

    mCreatureIDMap.clear();
}

// this allocates space for the daughter in the cell map,
// but does not enter it into any lists or change the parent.
Creature*
World::allocateSpaceForOffspring(const Creature& inParent, u_int32_t inDaughterLength)
{
    int32_t     attempts = 0;
    bool        foundLocation = false;
    address_t   location = -1;

    switch (mSettings.daughterAllocationStrategy())
    {
        case Settings::kRandomAlloc:
            {
                // Choose a random location within the addressing range
                while (attempts < kMaxMalAttempts)
                {
                    int32_t maxOffset = min((int32_t)mSoupSize, INT32_MAX);
                    u_int32_t offset = mRNG.IntegerC(-maxOffset, maxOffset);
                    location = (inParent.location() + offset) % mSoupSize;
                    
                    if (mCellMap->spaceAtAddress(location, inDaughterLength))
                    {
                        foundLocation = true;
                        break;
                    }
                    ++attempts;
                }
            }
            break;

        case Settings::kRandomPackedAlloc:
            {
                // Choose a random location within the addressing range
                int32_t maxOffset = min((int32_t)mSoupSize, INT32_MAX);
                u_int32_t offset = mRNG.IntegerC(-maxOffset, maxOffset);
                location = (inParent.location() + offset) % mSoupSize;
                
                foundLocation = mCellMap->searchForSpace(location, inDaughterLength, kMaxMalSearchRange, CellMap::kBothways);
            }
            break;

        case Settings::kClosestAlloc:
            {
                location = inParent.addressFromOffset(inParent.cpu().mRegisters[k_bx]);     // why bx?
                foundLocation = mCellMap->searchForSpace(location, inDaughterLength, kMaxMalSearchRange, CellMap::kBothways);
            }
            break;

        case Settings::kPreferredAlloc:
            {
                location = inParent.addressFromOffset(inParent.cpu().mRegisters[k_ax]);     // why ax?
                foundLocation = mCellMap->searchForSpace(location, inDaughterLength, kMaxMalSearchRange, CellMap::kBothways);
            }
            break;
    }

    Creature*   daughter = NULL;
    if (foundLocation)
    {
        daughter = createCreature();
        daughter->setLocation(location);
        daughter->setLength(inDaughterLength);
    
        bool added = mCellMap->insertCreature(daughter);
        BOOST_ASSERT(added);
#ifdef NDEBUG
        if (!added)
            mCellMap->printCreatures();
#endif
    }
    
    return daughter;
}

void
World::noteInstructionCopy()
{
    if (mCopyErrorPending)  // just did one
    {
        RandomLib::ExponentialDistribution<double> expDist;
        int32_t copyErrorDelay;
        do
        {
            copyErrorDelay = expDist(mRNG, mSettings.meanCopyErrorInterval());
        } while (copyErrorDelay <= 0);
        
        mNextCopyError = copyErrorDelay;
        mCopiesSinceLastError = 0;
        mCopyErrorPending = false;
    }
    else
    {
        ++mCopiesSinceLastError;
        mCopyErrorPending = (mCopiesSinceLastError == mNextCopyError);
    }
}


void
World::handleBirth(Creature* inParent, Creature* inChild)
{
    inChild->setSliceSize(mTimeSlicer.initialSliceSizeForCreature(inChild, mSettings.sizeSelection()));
    inChild->setReferencedLocation(inChild->location());

    // add to slicer and reaper
    creatureAdded(inChild);
    
    // collect metabolic data
    
    
    // collect genebank data
    
    
    // inherit leanness?


    bool bredTrue = inParent->gaveBirth(inChild);
    if (bredTrue)
    {
        InventoryGenotype* parentGenotype = NULL;

        // if the parent has not diverged, we could use its genotype. However, this may have changed
        // because of cosmic mutations, being written over etc, so we need to fetch it again.
        if (inParent->genotypeDivergence() == 0)
            parentGenotype = inParent->genotype();

        InventoryGenotype*   foundGenotype = NULL;
        if (mInventory->enterGenotype(inParent->genomeData(), foundGenotype))
        {
            // it's new
            foundGenotype->setOriginInstructions(inParent->originInstructions());
            foundGenotype->setOriginGenerations(inParent->generation());
            
//            cout << "New genotype: " << foundGenotype->genome().printableGenome() << endl;
//            cout << "      parent: " << (parentGenotype ? foundGenotype->genome().printableGenome() : "unclean") << endl;
        }
        else
        {
        }

        if (parentGenotype != foundGenotype)
        {
            if (parentGenotype)
            {
                // cout << "Creature genotype changed between birth and reproduction:" << endl;
                // cout << "was: " << parentGenotype->name() << " " << parentGenotype->printableGenome() << endl;
                // cout << "now: " << foundGenotype->name() << " " << foundGenotype->printableGenome() << endl;
                // old genotype lost a member
                parentGenotype->creatureDied();
            }

            inParent->setGenotype(foundGenotype);
            inParent->setGenotypeDivergence(0);
            foundGenotype->creatureBorn();  // count the parent
        }

        inChild->setGenotype(foundGenotype);
        inChild->setGenotypeDivergence(0);
        foundGenotype->creatureBorn();  // count the child
    }
    else
    {
        // not bred true
        inChild->setGenotype(inParent->genotype());
        inChild->setGenotypeDivergence(inParent->genotypeDivergence() + 1);
    }
    
    inChild->onBirth(*this);
}

void
World::handleDeath(Creature* inCreature)
{
    inCreature->onDeath(*this);

    if (inCreature->genotypeDivergence() == 0)
        inCreature->genotype()->creatureDied();

    eradicateCreature(inCreature);
}

int32_t
World::instructionFlaw(u_int64_t inInstructionCount)
{
    int32_t theFlaw = mRNG.Boolean() ? 1 : -1;

    RandomLib::ExponentialDistribution<double> expDist;
    int64_t flawDelay;
    do 
    {
        flawDelay = static_cast<int64_t>(expDist(mRNG, mSettings.meanFlawInterval()));
    } while (flawDelay <= 0);

    mNextFlawInstruction = inInstructionCount + flawDelay;
    
    return theFlaw;
}

void
World::cosmicRay(u_int64_t inInstructionCount)
{
    address_t   target = mRNG.Integer(mSoupSize);

    instruction_t inst = mSoup->instructionAtAddress(target);
    inst = mutateInstruction(inst, mSettings.mutationType());
    mSoup->setInstructionAtAddress(target, inst);
    
    RandomLib::ExponentialDistribution<double> expDist;
    int64_t cosmicDelay;
    do
    {
        cosmicDelay = static_cast<int64_t>(expDist(mRNG, mSettings.meanCosmicTimeInterval()));
    } while (cosmicDelay <= 0);

    mNextCosmicRayInstruction = inInstructionCount + cosmicDelay;
}

void
World::creatureAdded(Creature* inCreature)
{
    BOOST_ASSERT(inCreature->soup() == mSoup);

    mCreatureIDMap[inCreature->creatureID()] = inCreature;
    
    mTimeSlicer.insertCreature(*inCreature);
    mReaper.addCreature(*inCreature);
}

void
World::creatureRemoved(Creature* inCreature)
{
    BOOST_ASSERT(inCreature && inCreature->soup() == mSoup);

    mReaper.removeCreature(*inCreature);
    mTimeSlicer.removeCreature(*inCreature);

    mCreatureIDMap.erase(inCreature->creatureID());
}

#pragma mark -

// Settings

void
World::setSettings(const Settings& inSettings)
{
    mSettings = inSettings;
    // FIXME: recompute next flaw, copy error, cosmic ray times
}

#pragma mark -

// static
std::string
World::xmlStringFromWorld(const World* inWorld)
{
    std::ostringstream stringStream;
    
    ::boost::archive::xml_oarchive xmlArchive(stringStream);
    xmlArchive << BOOST_SERIALIZATION_NVP(inWorld);
    
    return stringStream.str();
}

// static
World*
World::worldFromXMLString(const std::string& inString)
{
    std::istringstream stringStream(inString);

    ::boost::archive::xml_iarchive xmlArchive(stringStream);

    World* braveNewWorld;
    xmlArchive >> BOOST_SERIALIZATION_NVP(braveNewWorld);
    return braveNewWorld;
}

// static
std::string
World::dataFromWorld(const World* inWorld)
{
    std::ostringstream stringStream;
    
    ::boost::archive::binary_oarchive binaryArchive(stringStream);
    binaryArchive << BOOST_SERIALIZATION_NVP(inWorld);
    
    return stringStream.str();
}

// static
World*
World::worldFromData(const std::string& inString)
{
    std::istringstream stringStream(inString);

    ::boost::archive::binary_iarchive binaryArchive(stringStream);

    World* braveNewWorld;
    binaryArchive >> BOOST_SERIALIZATION_NVP(braveNewWorld);
    return braveNewWorld;
}

} // namespace MacTierra
