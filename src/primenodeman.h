
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef PRIMENODEMAN_H
#define PRIMENODEMAN_H

#include "bignum.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "core.h"
#include "util.h"
#include "script.h"
#include "base58.h"
#include "main.h"
#include "primenode.h"

#define PRIMENODES_DUMP_SECONDS               (15*60)
#define PRIMENODES_DSEG_SECONDS               (3*60*60)

using namespace std;

class CPrimenodeMan;

extern CPrimenodeMan mnodeman;

extern void Misbehaving(NodeId nodeid, int howmuch);

void DumpPrimenodes();

/** Access to the MN database (mncache.dat) */
class CPrimenodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;
public:
    enum ReadResult {
        Ok,
       FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CPrimenodeDB();
    bool Write(const CPrimenodeMan &mnodemanToSave);
    ReadResult Read(CPrimenodeMan& mnodemanToLoad);
};

class CPrimenodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // map to hold all MNs
    std::vector<CPrimenode> vPrimenodes;
    // who's asked for the primenode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForPrimenodeList;
    // who we asked for the primenode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForPrimenodeList;
    // which primenodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForPrimenodeListEntry;

public:
    // keep track of dsq count to prevent primenodes from gaming darksend queue
    int64_t nDsqCount;

    IMPLEMENT_SERIALIZE
    (
        // serialized format:
        // * version byte (currently 0)
        // * primenodes vector
        {
                LOCK(cs);
                unsigned char nVersion = 0;
                READWRITE(nVersion);
                READWRITE(vPrimenodes);
                READWRITE(mAskedUsForPrimenodeList);
                READWRITE(mWeAskedForPrimenodeList);
                READWRITE(mWeAskedForPrimenodeListEntry);
                READWRITE(nDsqCount);
        }
    )

    CPrimenodeMan();
    CPrimenodeMan(CPrimenodeMan& other);

    // Add an entry
    bool Add(CPrimenode &mn);

    // Check all primenodes
    void Check();

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, CTxIn &vin);

    // Check all primenodes and remove inactive
    void CheckAndRemove();

    // Clear primenode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    int CountPrimenodesAboveProtocol(int protocolVersion);

    void DsegUpdate(CNode* pnode);

    // Find an entry
    CPrimenode* Find(const CTxIn& vin);
    CPrimenode* Find(const CPubKey& pubKeyPrimenode);

    //Find an entry thta do not match every entry provided vector
    CPrimenode* FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge);

    // Find a random entry
    CPrimenode* FindRandom();

    /// Find a random entry
    CPrimenode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

    // Get the current winner for this block
    CPrimenode* GetCurrentPrimeNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

    std::vector<CPrimenode> GetFullPrimenodeVector() { Check(); return vPrimenodes; }

    std::vector<pair<int, CPrimenode> > GetPrimenodeRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetPrimenodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CPrimenode* GetPrimenodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void ProcessPrimenodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    // Return the number of (unique) primenodes
    int size() { return vPrimenodes.size(); }

    std::string ToString() const;

    //
    // Relay Primenode Messages
    //

    void RelayOldPrimenodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion);
    void RelayPrimenodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion, CScript rewardAddress, int rewardPercentage);
    void RelayPrimenodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop);

    void Remove(CTxIn vin);

};

#endif
