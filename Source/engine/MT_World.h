/*
 *  MT_World.h
 *  MacTierra
 *
 *  Created by Simon Fraser on 8/10/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef MT_World_h
#define MT_World_h

#include <map>

#include <boost/serialization/export.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/serialization.hpp>

#include <boost/thread.hpp>

#define HAVE_BOOST_SERIALIZATION 1
#include <RandomLib/Random.hpp>

#include "MT_Engine.h"

#include "MT_CellMap.h"
#include "MT_DataCollection.h"
#include "MT_ExecutionUnit.h"
#include "MT_ExecutionUnit0.h"      // needed for serialization registration
#include "MT_Inventory.h"
#include "MT_Reaper.h"
#include "MT_Settings.h"
#include "MT_Soup.h"
#include "MT_Timeslicer.h"

namespace MacTierra {

class Creature;

class World
{
friend class ExecutionUnit0;
public:

    World();
    ~World();

    void                initializeSoup(u_int32_t inSoupSize);

    u_int32_t           soupSize() const    { return mSoupSize; }

    Soup*               soup() const        { return mSoup; }
    CellMap*            cellMap() const     { return mCellMap; }

    const TimeSlicer&   timeSlicer() const { return mTimeSlicer; }
    const Reaper&       reaper() const  { return mReaper; }
    
    Inventory*          inventory() const   { return mInventory; }

    DataCollector*      dataCollector() const   { return mDataCollector; }
    
    Creature*           createCreature();
    void                eradicateCreature(Creature* inCreature);
    
    Creature*           insertCreature(address_t inAddress, const instruction_t* inInstructions, u_int32_t inLength);
    
    void                iterate(u_int32_t inNumCycles);
    // execute one cycle for the current creature; at the end if its slice, execute all other creatures
    // and then step the same creature again
    void                stepCreature(Creature* inCreature);

    RandomLib::Random&  RNG()   { return mRNG; }

    bool                copyErrorPending() const { return mCopyErrorPending; }

    instruction_t       mutateInstruction(instruction_t inInst, Settings::EMutationType inMutationType) const;

    // settings
    const Settings&     settings() const { return mSettings; }
    void                setSettings(const Settings& inSettings);

    void                setInitialRandomSeed(u_int32_t inIntialSeed);
    u_int32_t           initialRandomSeed() const;

    // data
    u_int32_t           numAdultCreatures() const;
    double              meanCreatureSize() const;   // counts adults only
    
    void                printCreatures() const;

    enum EWorldSerializationFormat {
        kBinary,
        kXML
    };
    
    // save/restore
    static void         worldToStream(const World* inWorld, std::ostream& inStream, EWorldSerializationFormat inFormat);
    static World*       worldFromStream(std::istream& inStream, EWorldSerializationFormat inFormat);

    
protected:

    creature_id     uniqueCreatureID();

    void            destroyCreatures();

    // handle the 'mal' instruction
    Creature*       allocateSpaceForOffspring(const Creature& inParent, u_int32_t inDaughterLength);

    bool            timeForDataCollection(u_int64_t inInstructionCount) const
    {
        return (mDataCollector && mDataCollector->nextCollectionInstructions() == inInstructionCount);
    }

    bool            timeForFlaw(u_int64_t inInstructionCount) const
    {
        return (mSettings.flawRate() > 0.0 && inInstructionCount == mNextFlawInstruction);
    }
    
    bool            timeForCosmicRay(u_int64_t inInstructionCount) const
    {
        return (mSettings.cosmicRate() > 0.0 && inInstructionCount == mNextCosmicRayInstruction);
    }
    
    void            noteInstructionCopy();
    
    // birth happens on 'divide'
    void            handleBirth(Creature* inParent, Creature* inChild);
    void            handleDeath(Creature* inCreature);

    int32_t         instructionFlaw(u_int64_t inInstructionCount);
    void            computeNextInstructionFlaw(u_int64_t inInstructionCount);

    void            cosmicRay(u_int64_t inInstructionCount);
    void            computeNextCosmicRay(u_int64_t inInstructionCount);

    void            computeNextCopyError();
    
    // these add and remove from the time slicer and reaper queues.
    void            creatureAdded(Creature* inCreature);
    void            creatureRemoved(Creature* inCreature);

private:
    friend class ::boost::serialization::access;
    template<class Archive> void serialize(Archive& ar, const unsigned int version)
    {
        // using BOOST_CLASS_EXPORT_GUID() for these causes a crash on quit
        ar.register_type(static_cast<ExecutionUnit0 *>(NULL));
        ar.register_type(static_cast<InventoryGenotype *>(NULL));

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("settings", mSettings);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("random_number_generator", mRNG);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("soup_size", mSoupSize);
        
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("soup", mSoup);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("cell_map", mCellMap);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("next_creature_id", mNextCreatureID);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("creature_id_map", mCreatureIDMap);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("isa", mExecution);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("timeslicer", mTimeSlicer);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("reaper", mReaper);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("inventory", mInventory);
        
        // serialize the mDataCollector?

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("cur_creature_cycles", mCurCreatureCycles);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("cur_creature_slice_cycles", mCurCreatureSliceCycles);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("copy_error_pending", mCopyErrorPending);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("copies_since_last_error", mCopiesSinceLastError);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("next_copy_error_time", mNextCopyError);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("next_flaw_time", mNextFlawInstruction);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("next_cosmic_ray_time", mNextCosmicRayInstruction);
    }


protected:

    Settings            mSettings;
    
    mutable RandomLib::Random   mRNG;

    u_int32_t           mSoupSize;

    Soup*               mSoup;
    CellMap*            mCellMap;

    // creature book keeping
    creature_id         mNextCreatureID;

    // creatures hashed by ID
    typedef std::map<creature_id, Creature*>    CreatureIDMap;
    CreatureIDMap       mCreatureIDMap;
    
    ExecutionUnit*  mExecution;
    
    TimeSlicer      mTimeSlicer;

    Reaper          mReaper;
    
    Inventory*      mInventory;
    
    DataCollector*  mDataCollector;

    // runtime
    u_int32_t       mCurCreatureCycles;         // fAlive
    u_int32_t       mCurCreatureSliceCycles;    // fCurCpuSliceSize

    bool            mCopyErrorPending;
    u_int32_t       mCopiesSinceLastError;
    u_int32_t       mNextCopyError;

    u_int64_t       mNextFlawInstruction;
    u_int64_t       mNextCosmicRayInstruction;    
};







} // namespace MacTierra

#endif // MT_World_h
