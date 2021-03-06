/*
 *  MT_Inventory.h
 *  MacTierra
 *
 *  Created by Simon Fraser on 8/18/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */


#ifndef MT_Inventory_h
#define MT_Inventory_h

#include <map>
#include <vector>

#include <boost/assert.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/serialization.hpp>

#include <wtf/Noncopyable.h>

#include "MT_Engine.h"
#include "MT_Genotype.h"

namespace MacTierra {


class InventoryGenotype : public Genotype
{
friend class Inventory;
public:
    InventoryGenotype(const std::string& inIdentifier, const GenomeData& inGenotype);
    
    u_int32_t       numberAlive() const         { return mNumAlive; }
    u_int32_t       numberEverLived() const     { return mNumEverLived; }

    u_int64_t       originInstructions() const  { return mOriginInstructions; }
    void            setOriginInstructions(u_int64_t inInstCount) { mOriginInstructions = inInstCount; }

    u_int32_t       originGenerations() const  { return mOriginGenerations; }
    void            setOriginGenerations(u_int32_t inGenerations) { mOriginGenerations = inGenerations; }

private:

    void creatureBorn()
    {
        ++mNumAlive;
        ++mNumEverLived;
    }

    void creatureDied() 
    {
        BOOST_ASSERT(mNumAlive > 0);
        --mNumAlive;
    }

private:

    InventoryGenotype() // default ctor for serialization
    : mNumAlive(0)
    , mNumEverLived(0)
    , mOriginInstructions(0)
    , mOriginGenerations(0)
    {
    }

    friend class ::boost::serialization::access;
    template<class Archive> void serialize(Archive& ar, const unsigned int version)
    {
        ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Genotype);
        
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("num_alive", mNumAlive);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("num_ever", mNumEverLived);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("origin_time", mOriginInstructions);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("origin_generations", mOriginGenerations);
    }

protected:

    u_int32_t       mNumAlive;
    u_int32_t       mNumEverLived;
    
    u_int64_t       mOriginInstructions;
    u_int32_t       mOriginGenerations;
};

} // namespace MacTierra

//BOOST_CLASS_EXPORT_GUID(MacTierra::InventoryGenotype, "InventoryGenotype")


namespace MacTierra {

class InventoryListener;

// The inventory tracks the species that are alive now.
class Inventory : Noncopyable
{
public:
    typedef std::map<GenomeData, InventoryGenotype*> InventoryMap;
    typedef std::multimap<u_int32_t, InventoryGenotype*>  SizeMap;
    typedef std::vector<InventoryListener*> ListenerVector;

    Inventory();
    ~Inventory();

    InventoryGenotype*  findGenotype(const GenomeData& inGenotype) const;
    
    // return true if it's new
    bool                enterGenotype(const GenomeData& inGenotype, InventoryGenotype*& outGenotype);

    void                creatureBorn(InventoryGenotype* inGenotype);
    void                creatureDied(InventoryGenotype* inGenotype);
    
    void                printCreatures() const;
    
    const InventoryMap& inventoryMap() const { return mInventoryMap; }

    void                writeToStream(std::ostream& inStream) const;

    void                setListenerAliveThreshold(u_int32_t inThreshold)    { mListenerAliveThreshold = inThreshold; }
    u_int32_t           listenerAliveThreshold() const                      { return mListenerAliveThreshold; }

    void                registerListener(InventoryListener* inListener);
    void                unregisterListener(InventoryListener* inListener);
    
protected:

    std::string         uniqueIdentifierForLength(u_int32_t inLength) const;

    void                notifyListenersForGenotype(const InventoryGenotype* inGenotype);

private:
    friend class ::boost::serialization::access;
    template<class Archive> void serialize(Archive& ar, const unsigned int version)
    {
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("total_species", mNumSpeciesEver);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("current_species", mNumSpeciesCurrent);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("speciation", mSpeciationCount);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("extinction", mExtinctionCount);

        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("map", mInventoryMap);
        ar & MT_BOOST_MEMBER_SERIALIZATION_NVP("size_map", mGenotypeSizeMap);
    }
    
protected:

    u_int32_t       mNumSpeciesEver;
    u_int32_t       mNumSpeciesCurrent;

    u_int32_t       mSpeciationCount;
    u_int32_t       mExtinctionCount;

    InventoryMap    mInventoryMap;
    SizeMap         mGenotypeSizeMap;
    
    // members below here not archived
    u_int32_t       mListenerAliveThreshold;
    ListenerVector  mListeners;

    // remember which genotypes we've notified the listener about
    typedef std::map<GenomeData, const InventoryGenotype*> NotifiedGenotypeMap;
    NotifiedGenotypeMap    mListenerNotifiedGenotypes;

};

} // namespace MacTierra

#endif // MT_Inventory_h
