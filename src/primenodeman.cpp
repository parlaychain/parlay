#include "primenodeman.h"
#include "primenode.h"
#include "activeprimenode.h"
#include "darksend.h"
#include "core.h"
#include "util.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>


/** Primenode manager */
CPrimenodeMan mnodeman;
CCriticalSection cs_process_message;

struct CompareValueOnly
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};
struct CompareValueOnlyMN
{
    bool operator()(const pair<int64_t, CPrimenode>& t1,
                    const pair<int64_t, CPrimenode>& t2) const
    {
        return t1.first < t2.first;
    }
};


//
// CPrimenodeDB
//

CPrimenodeDB::CPrimenodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "PrimenodeCache";
}

bool CPrimenodeDB::Write(const CPrimenodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPrimenodes(SER_DISK, CLIENT_VERSION);
    ssPrimenodes << strMagicMessage; // primenode cache file specific magic message
    ssPrimenodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssPrimenodes << mnodemanToSave;
    uint256 hash = Hash(ssPrimenodes.begin(), ssPrimenodes.end());
    ssPrimenodes << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssPrimenodes;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    LogPrintf("Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", mnodemanToSave.ToString());

    return true;
}

CPrimenodeDB::ReadResult CPrimenodeDB::Read(CPrimenodeMan& mnodemanToLoad)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein = CAutoFile(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssPrimenodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPrimenodes.begin(), ssPrimenodes.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (primenode cache file specific magic message) and ..

        ssPrimenodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid primenode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssPrimenodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize address data into one CMnList object
        ssPrimenodes >> mnodemanToLoad;
    }
    catch (std::exception &e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    mnodemanToLoad.CheckAndRemove(); // clean out expired
    LogPrintf("Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", mnodemanToLoad.ToString());

    return Ok;
}

void DumpPrimenodes()
{
    int64_t nStart = GetTimeMillis();

    CPrimenodeDB mndb;
    CPrimenodeMan tempMnodeman;

    LogPrintf("Verifying mncache.dat format...\n");
    CPrimenodeDB::ReadResult readResult = mndb.Read(tempMnodeman);
    // there was an error and it was not an error on file openning => do not proceed
    if (readResult == CPrimenodeDB::FileError)
        LogPrintf("Missing primenode list file - mncache.dat, will try to recreate\n");
    else if (readResult != CPrimenodeDB::Ok)
    {
        LogPrintf("Error reading mncache.dat: ");
        if(readResult == CPrimenodeDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrintf("Primenode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CPrimenodeMan::CPrimenodeMan() {
    nDsqCount = 0;
}

bool CPrimenodeMan::Add(CPrimenode &mn)
{
    LOCK(cs);

    CPrimenode *pmn = Find(mn.vin);

    if (pmn == NULL)
    {
        LogPrint("primenode", "CPrimenodeMan: Adding new primenode %s - %i now\n", mn.addr.ToString().c_str(), size() + 1);
        vPrimenodes.push_back(mn);
        return true;
    }

    return false;
}

void CPrimenodeMan::AskForMN(CNode* pnode, CTxIn &vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForPrimenodeListEntry.find(vin.prevout);
    if (i != mWeAskedForPrimenodeListEntry.end())
    {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrintf("CPrimenodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + PRIMENODE_MIN_DSEEP_SECONDS;
    mWeAskedForPrimenodeListEntry[vin.prevout] = askAgain;
}

void CPrimenodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH(CPrimenode& mn, vPrimenodes)
        mn.Check();
}

void CPrimenodeMan::CheckAndRemove()
{
    LOCK(cs);

    Check();

    //remove inactive
    vector<CPrimenode>::iterator it = vPrimenodes.begin();
    while(it != vPrimenodes.end()){
        if((*it).activeState == CPrimenode::PRIMENODE_REMOVE || (*it).activeState == CPrimenode::PRIMENODE_VIN_SPENT || (*it).protocolVersion < nPrimenodeMinProtocol){
            LogPrint("primenode", "CPrimenodeMan: Removing inactive primenode %s - %i now\n", (*it).addr.ToString().c_str(), size() - 1);
            it = vPrimenodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the primenode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForPrimenodeList.begin();
    while(it1 != mAskedUsForPrimenodeList.end()){
        if((*it1).second < GetTime()) {
            mAskedUsForPrimenodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the primenode list
    it1 = mWeAskedForPrimenodeList.begin();
    while(it1 != mWeAskedForPrimenodeList.end()){
        if((*it1).second < GetTime()){
            mWeAskedForPrimenodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which primenodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForPrimenodeListEntry.begin();
    while(it2 != mWeAskedForPrimenodeListEntry.end()){
        if((*it2).second < GetTime()){
            mWeAskedForPrimenodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

}

void CPrimenodeMan::Clear()
{
    LOCK(cs);
    vPrimenodes.clear();
    mAskedUsForPrimenodeList.clear();
    mWeAskedForPrimenodeList.clear();
    mWeAskedForPrimenodeListEntry.clear();
    nDsqCount = 0;
}

int CPrimenodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? primenodePayments.GetMinPrimenodePaymentsProto() : protocolVersion;

    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {
        mn.Check();
        if(mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

int CPrimenodeMan::CountPrimenodesAboveProtocol(int protocolVersion)
{
    int i = 0;

    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {
        mn.Check();
        if(mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CPrimenodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    std::map<CNetAddr, int64_t>::iterator it = mWeAskedForPrimenodeList.find(pnode->addr);
    if (it != mWeAskedForPrimenodeList.end())
    {
        if (GetTime() < (*it).second) {
            LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
            return;
        }
    }
    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + PRIMENODES_DSEG_SECONDS;
    mWeAskedForPrimenodeList[pnode->addr] = askAgain;
}

CPrimenode *CPrimenodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CPrimenode& mn, vPrimenodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CPrimenode* CPrimenodeMan::FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge)
{
    LOCK(cs);

    CPrimenode *pOldestPrimenode = NULL;

    BOOST_FOREACH(CPrimenode &mn, vPrimenodes)
    {   
        mn.Check();
        if(!mn.IsEnabled()) continue;

        if(mn.GetPrimenodeInputAge() < nMinimumAge) continue;

        bool found = false;
        BOOST_FOREACH(const CTxIn& vin, vVins)
            if(mn.vin.prevout == vin.prevout)
            {   
                found = true;
                break;
            }
        if(found) continue;

        if(pOldestPrimenode == NULL || pOldestPrimenode->SecondsSincePayment() < mn.SecondsSincePayment())
        {
            pOldestPrimenode = &mn;
        }
    }

    return pOldestPrimenode;
}

CPrimenode *CPrimenodeMan::FindRandom()
{
    LOCK(cs);

    if(size() == 0) return NULL;

    return &vPrimenodes[GetRandInt(vPrimenodes.size())];
}

CPrimenode *CPrimenodeMan::Find(const CPubKey &pubKeyPrimenode)
{
    LOCK(cs);

    BOOST_FOREACH(CPrimenode& mn, vPrimenodes)
    {
        if(mn.pubkey2 == pubKeyPrimenode)
            return &mn;
    }
    return NULL;
}

CPrimenode *CPrimenodeMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? primenodePayments.GetMinPrimenodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrintf("CPrimenodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if(nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrintf("CPrimenodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH(CPrimenode &mn, vPrimenodes) {
        if(mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH(CTxIn &usedVin, vecToExclude) {
            if(mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if(found) continue;
        if(--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CPrimenode* CPrimenodeMan::GetCurrentPrimeNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    unsigned int score = 0;
    CPrimenode* winner = NULL;

    // scan for winner
    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {
        mn.Check();
        if(mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each primenode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        unsigned int n2 = 0;
        memcpy(&n2, &n, sizeof(n2));

        // determine the winner
        if(n2 > score){
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CPrimenodeMan::GetPrimenodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<unsigned int, CTxIn> > vecPrimenodeScores;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {

        if(mn.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        unsigned int n2 = 0;
        memcpy(&n2, &n, sizeof(n2));

        vecPrimenodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecPrimenodeScores.rbegin(), vecPrimenodeScores.rend(), CompareValueOnly());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(unsigned int, CTxIn)& s, vecPrimenodeScores){
        rank++;
        if(s.second == vin) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CPrimenode> > CPrimenodeMan::GetPrimenodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<unsigned int, CPrimenode> > vecPrimenodeScores;
    std::vector<pair<int, CPrimenode> > vecPrimenodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return vecPrimenodeRanks;

    // scan for winner
    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {

        mn.Check();

        if(mn.protocolVersion < minProtocol) continue;
        if(!mn.IsEnabled()) {
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        unsigned int n2 = 0;
        memcpy(&n2, &n, sizeof(n2));

        vecPrimenodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecPrimenodeScores.rbegin(), vecPrimenodeScores.rend(), CompareValueOnlyMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(unsigned int, CPrimenode)& s, vecPrimenodeScores){
        rank++;
        vecPrimenodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecPrimenodeRanks;
}

CPrimenode* CPrimenodeMan::GetPrimenodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<unsigned int, CTxIn> > vecPrimenodeScores;

    // scan for winner
    BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {

        if(mn.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        unsigned int n2 = 0;
        memcpy(&n2, &n, sizeof(n2));

        vecPrimenodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecPrimenodeScores.rbegin(), vecPrimenodeScores.rend(), CompareValueOnly());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(unsigned int, CTxIn)& s, vecPrimenodeScores){
        rank++;
        if(rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CPrimenodeMan::ProcessPrimenodeConnections()
{
    LOCK(cs_vNodes);

    if(!darkSendPool.pSubmittedToPrimenode) return;
    
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(darkSendPool.pSubmittedToPrimenode->addr == pnode->addr) continue;

        if(pnode->fDarkSendMaster){
            LogPrintf("Closing primenode connection %s \n", pnode->addr.ToString().c_str());
            pnode->CloseSocketDisconnect();
        }
    }
}

void CPrimenodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    //Normally would disable functionality, NEED this enabled for staking.
    //if(fLiteMode) return;

    if(!darkSendPool.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "dsee") { //DarkSend Election Entry

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        std::string strMessage;
        CScript rewardAddress = CScript();
        int rewardPercentage = 0;
        
        // 70047 and greater
        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion;

        //Invalid nodes check
        if (sigTime < 1510630200) {
            //LogPrintf("dsee - Bad packet\n");
            return;
        }
        
        if (sigTime > lastUpdated) {
            //LogPrintf("dsee - Bad node entry\n");
            return;
        }
        
        if (addr.GetPort() == 0) {
            //LogPrintf("dsee - Bad port\n");
            return;
        }
        
        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("dsee - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        bool isLocal = addr.IsRFC1918() || addr.IsLocal();
        //if(RegTest()) isLocal = false;

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);    

        if(protocolVersion < MIN_POOL_PEER_PROTO_VERSION) {
            LogPrintf("dsee - ignoring outdated primenode %s protocol version %d\n", vin.ToString().c_str(), protocolVersion);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript.SetDestination(pubkey.GetID());

        if(pubkeyScript.size() != 25) {
            LogPrintf("dsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2.SetDestination(pubkey2.GetID());

        if(pubkeyScript2.size() != 25) {
            LogPrintf("dsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if(!vin.scriptSig.empty()) {
            LogPrintf("dsee - Ignore Not Empty ScriptSig %s\n",vin.ToString().c_str());
            return;
        }

        std::string errorMessage = "";
        if(!darkSendSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)){
            LogPrintf("dsee - Got bad primenode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        //search existing primenode list, this is where we update existing primenodes with new dsee broadcasts
        CPrimenode* pmn = this->Find(vin);
        // if we are a primenode but with undefined vin and this dsee is ours (matches our Primenode privkey) then just skip this part
        if(pmn != NULL && !(fPrimeNode && activePrimenode.vin == CTxIn() && pubkey2 == activePrimenode.pubKeyPrimenode))
        {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if(count == -1 && pmn->pubkey == pubkey && !pmn->UpdatedWithin(PRIMENODE_MIN_DSEE_SECONDS)){
                pmn->UpdateLastSeen();
                if(pmn->sigTime < sigTime){ //take the newest entry
                    if (!CheckNode((CAddress)addr)){
                        pmn->isPortOpen = false;
                    } else {
                        pmn->isPortOpen = true;
                        addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
                    }
                    LogPrintf("dsee - Got updated entry for %s\n", addr.ToString().c_str());
                    pmn->pubkey2 = pubkey2;
                    pmn->sigTime = sigTime;
                    pmn->sig = vchSig;
                    pmn->protocolVersion = protocolVersion;
                    pmn->addr = addr;
                    pmn->Check();
                    pmn->isOldNode = true;
                    if(pmn->IsEnabled())
                        mnodeman.RelayOldPrimenodeEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
                }
            }

            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the primenode
        //  - this is expensive, so it's only done once per primenode
        if(!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrintf("dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        LogPrint("primenode", "dsee - Got NEW primenode entry %s\n", addr.ToString().c_str());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckDarkSendPool()

        CValidationState state;
        CTransaction tx = CTransaction();
        CTxOut vout = CTxOut((GetMNCollateral(pindexBest->nHeight)-1)*COIN, darkSendPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);
        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if(!lockMain) return;
            fAcceptable = AcceptableInputs(mempool, tx, false, NULL);
        }
        if(fAcceptable){
            LogPrint("primenode", "dsee - Accepted primenode entry %i %i\n", count, current);

            if(GetInputAge(vin) < PRIMENODE_MIN_CONFIRMATIONS){
                LogPrintf("dsee - Input must have least %d confirmations\n", PRIMENODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 10000 TansferCoin tx got PRIMENODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            GetTransaction(vin.prevout.hash, tx, hashBlock);
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second)
            {
                CBlockIndex* pMNIndex = (*mi).second; // block for 10000 TansferCoin tx -> 1 confirmation
                CBlockIndex* pConfIndex = FindBlockByHeight((pMNIndex->nHeight + PRIMENODE_MIN_CONFIRMATIONS - 1)); // block where tx got PRIMENODE_MIN_CONFIRMATIONS
                if(pConfIndex->GetBlockTime() > sigTime)
                {
                    LogPrintf("dsee - Bad sigTime %d for primenode %20s %105s (%i conf block is at %d)\n",
                              sigTime, addr.ToString(), vin.ToString(), PRIMENODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // add our primenode
            CPrimenode mn(addr, vin, pubkey, vchSig, sigTime, pubkey2, protocolVersion, rewardAddress, rewardPercentage);
            mn.UpdateLastSeen(lastUpdated);

            if (!CheckNode((CAddress)addr)){
                mn.ChangePortStatus(false);
            } else {
                addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
            }
            
            mn.ChangeNodeStatus(true);
            this->Add(mn);

            // if it matches our primenodeprivkey, then we've been remotely activated
            if(pubkey2 == activePrimenode.pubKeyPrimenode && protocolVersion == PROTOCOL_VERSION){
                activePrimenode.EnableHotColdPrimeNode(vin, addr);
                }

            if(count == -1 && !isLocal)
                mnodeman.RelayOldPrimenodeEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
            
        } else {
            LogPrintf("dsee - Rejected primenode entry %s\n", addr.ToString().c_str());

            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LogPrintf("dsee - %s from %s %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "dsee+") { //DarkSend Election Entry+

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript rewardAddress;
        int rewardPercentage;
        std::string strMessage;

        // 70047 and greater
        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> rewardAddress >> rewardPercentage;

        //Invalid nodes check
        if (sigTime < 1510630200) {
            //LogPrintf("dsee+ - Bad packet\n");
            return;
        }
        
        if (sigTime > lastUpdated) {
            //LogPrintf("dsee+ - Bad node entry\n");
            return;
        }
        
        if (addr.GetPort() == 0) {
            //LogPrintf("dsee+ - Bad port\n");
            return;
        }
        
        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("dsee+ - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        bool isLocal = addr.IsRFC1918() || addr.IsLocal();
        //if(RegTest()) isLocal = false;

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion)  + rewardAddress.ToString() + boost::lexical_cast<std::string>(rewardPercentage);
        
        if(rewardPercentage < 0 || rewardPercentage > 100){
            LogPrintf("dsee+ - reward percentage out of range %d\n", rewardPercentage);
            return;
        }
        if(protocolVersion < MIN_POOL_PEER_PROTO_VERSION) {
            LogPrintf("dsee+ - ignoring outdated primenode %s protocol version %d\n", vin.ToString().c_str(), protocolVersion);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript.SetDestination(pubkey.GetID());

        if(pubkeyScript.size() != 25) {
            LogPrintf("dsee+ - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2.SetDestination(pubkey2.GetID());

        if(pubkeyScript2.size() != 25) {
            LogPrintf("dsee+ - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if(!vin.scriptSig.empty()) {
            LogPrintf("dsee+ - Ignore Not Empty ScriptSig %s\n",vin.ToString().c_str());
            return;
        }

        std::string errorMessage = "";
        if(!darkSendSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)){
            LogPrintf("dsee+ - Got bad primenode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        //search existing primenode list, this is where we update existing primenodes with new dsee broadcasts
        CPrimenode* pmn = this->Find(vin);
        // if we are a primenode but with undefined vin and this dsee is ours (matches our Primenode privkey) then just skip this part
        if(pmn != NULL && !(fPrimeNode && activePrimenode.vin == CTxIn() && pubkey2 == activePrimenode.pubKeyPrimenode))
        {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if(count == -1 && pmn->pubkey == pubkey && !pmn->UpdatedWithin(PRIMENODE_MIN_DSEE_SECONDS)){
                pmn->UpdateLastSeen();

                if(pmn->sigTime < sigTime){ //take the newest entry
                    if (!CheckNode((CAddress)addr)){
                        pmn->isPortOpen = false;
                    } else {
                        pmn->isPortOpen = true;
                        addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
                    }
                    LogPrintf("dsee+ - Got updated entry for %s\n", addr.ToString().c_str());
                    pmn->pubkey2 = pubkey2;
                    pmn->sigTime = sigTime;
                    pmn->sig = vchSig;
                    pmn->protocolVersion = protocolVersion;
                    pmn->addr = addr;
                    pmn->rewardAddress = rewardAddress;
                    pmn->rewardPercentage = rewardPercentage;                    
                    pmn->Check();
                    pmn->isOldNode = false;
                    if(pmn->IsEnabled())
                        mnodeman.RelayPrimenodeEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, rewardAddress, rewardPercentage);
                }
            }

            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the primenode
        //  - this is expensive, so it's only done once per primenode
        if(!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrintf("dsee+ - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        LogPrint("primenode", "dsee+ - Got NEW primenode entry %s\n", addr.ToString().c_str());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckDarkSendPool()

        CValidationState state;
        CTransaction tx = CTransaction();
        CTxOut vout = CTxOut((GetMNCollateral(pindexBest->nHeight)-1)*COIN, darkSendPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);
        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if(!lockMain) return;
            fAcceptable = AcceptableInputs(mempool, tx, false, NULL);
        }
        if(fAcceptable){
            LogPrint("primenode", "dsee+ - Accepted primenode entry %i %i\n", count, current);

            if(GetInputAge(vin) < PRIMENODE_MIN_CONFIRMATIONS){
                LogPrintf("dsee+ - Input must have least %d confirmations\n", PRIMENODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 10000 TansferCoin tx got PRIMENODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            GetTransaction(vin.prevout.hash, tx, hashBlock);
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
           if (mi != mapBlockIndex.end() && (*mi).second)
            {
                CBlockIndex* pMNIndex = (*mi).second; // block for 10000 TansferCoin tx -> 1 confirmation
                CBlockIndex* pConfIndex = FindBlockByHeight((pMNIndex->nHeight + PRIMENODE_MIN_CONFIRMATIONS - 1)); // block where tx got PRIMENODE_MIN_CONFIRMATIONS
                if(pConfIndex->GetBlockTime() > sigTime)
                {
                    LogPrintf("dsee+ - Bad sigTime %d for primenode %20s %105s (%i conf block is at %d)\n",
                              sigTime, addr.ToString(), vin.ToString(), PRIMENODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }


            //doesn't support multisig addresses
            if(rewardAddress.IsPayToScriptHash()){
                rewardAddress = CScript();
                rewardPercentage = 0;
            }

            // add our primenode
            CPrimenode mn(addr, vin, pubkey, vchSig, sigTime, pubkey2, protocolVersion, rewardAddress, rewardPercentage);
            mn.UpdateLastSeen(lastUpdated);

            if (!CheckNode((CAddress)addr)){
                mn.ChangePortStatus(false);
            } else {
                addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
            }
            
            mn.ChangeNodeStatus(false);
            this->Add(mn);
            
            // if it matches our primenodeprivkey, then we've been remotely activated
            if(pubkey2 == activePrimenode.pubKeyPrimenode && protocolVersion == PROTOCOL_VERSION){
                activePrimenode.EnableHotColdPrimeNode(vin, addr);
                }

            if(count == -1 && !isLocal)
                mnodeman.RelayPrimenodeEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, rewardAddress, rewardPercentage);

        } else {
            LogPrintf("dsee+ - Rejected primenode entry %s\n", addr.ToString().c_str());

            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LogPrintf("dsee+ - %s from %s %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }
    
    else if (strCommand == "dseep") { //DarkSend Election Entry Ping

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrintf("dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("dseep - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrintf("dseep - Signature rejected, too far into the past %s - %d %d \n", vin.ToString().c_str(), sigTime, GetAdjustedTime());
            return;
        }

        // see if we have this primenode
        CPrimenode* pmn = this->Find(vin);
        if(pmn != NULL && pmn->protocolVersion >= MIN_POOL_PEER_PROTO_VERSION)
        {
            // LogPrintf("dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if(pmn->lastDseep < sigTime)
            {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if(!darkSendSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
                {
                    LogPrintf("dseep - Got bad primenode address signature %s \n", vin.ToString().c_str());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                pmn->lastDseep = sigTime;

                if(!pmn->UpdatedWithin(PRIMENODE_MIN_DSEEP_SECONDS))
                {
                    if(stop) pmn->Disable();
                    else
                    {
                        pmn->UpdateLastSeen();
                        pmn->Check();
                        if(!pmn->IsEnabled()) return;
                    }
                    mnodeman.RelayPrimenodeEntryPing(vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("primenode", "dseep - Couldn't find primenode entry %s\n", vin.ToString().c_str());

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForPrimenodeListEntry.find(vin.prevout);
        if (i != mWeAskedForPrimenodeListEntry.end())
        {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // ask for the dsee info once from the node that sent dseep

        LogPrintf("dseep - Asking source node for missing entry %s\n", vin.ToString().c_str());
        pfrom->PushMessage("dseg", vin);
        int64_t askAgain = GetTime()+ PRIMENODE_MIN_DSEEP_SECONDS;
        mWeAskedForPrimenodeListEntry[vin.prevout] = askAgain;

    } else if (strCommand == "mvote") { //Primenode Vote

        CTxIn vin;
        vector<unsigned char> vchSig;
        int nVote;
        vRecv >> vin >> vchSig >> nVote;

        // see if we have this Primenode
        CPrimenode* pmn = this->Find(vin);
        if(pmn != NULL)
        {
            if((GetAdjustedTime() - pmn->lastVote) > (60*60))
            {
                std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nVote);

                std::string errorMessage = "";
                if(!darkSendSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
                {
                    LogPrintf("mvote - Got bad Primenode address signature %s \n", vin.ToString().c_str());
                    return;
                }

                pmn->nVote = nVote;
                pmn->lastVote = GetAdjustedTime();

                //send to all peers
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                    pnode->PushMessage("mvote", vin, vchSig, nVote);
            }

            return;
        }

    } else if (strCommand == "dseg") { //Get primenode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            if(!pfrom->addr.IsRFC1918() && Params().NetworkID() == CChainParams::MAIN)
            {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForPrimenodeList.find(pfrom->addr);
                if (i != mAskedUsForPrimenodeList.end())
                {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }

                int64_t askAgain = GetTime() + PRIMENODES_DSEG_SECONDS;
                mAskedUsForPrimenodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int count = this->size();
        int i = 0;

        BOOST_FOREACH(CPrimenode& mn, vPrimenodes) {

            if(mn.addr.IsRFC1918()) continue; //local network

            if(mn.IsEnabled())
            {
                LogPrint("primenode", "dseg - Sending primenode entry - %s \n", mn.addr.ToString().c_str());
                if(vin == CTxIn()){
                    if (mn.isOldNode){
                        pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                    } else {
                        pfrom->PushMessage("dsee+", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion, mn.rewardAddress, mn.rewardPercentage);
                    }
                } else if (vin == mn.vin) {
                    if (mn.isOldNode){
                        pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                    } else {
                        pfrom->PushMessage("dsee+", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion, mn.rewardAddress, mn.rewardPercentage);
                    }
                    LogPrintf("dseg - Sent 1 primenode entries to %s\n", pfrom->addr.ToString().c_str());
                    return;
                }
                i++;
            }
        }

        LogPrintf("dseg - Sent %d primenode entries to %s\n", i, pfrom->addr.ToString().c_str());
    }

}

void CPrimenodeMan::RelayOldPrimenodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        pnode->PushMessage("dsee", vin, addr, vchSig, nNow, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
    }
}

void CPrimenodeMan::RelayPrimenodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion, CScript rewardAddress, int rewardPercentage)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        pnode->PushMessage("dsee+", vin, addr, vchSig, nNow, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, rewardAddress, rewardPercentage);
    }
}

void CPrimenodeMan::RelayPrimenodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        pnode->PushMessage("dseep", vin, vchSig, nNow, stop);
}

void CPrimenodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CPrimenode>::iterator it = vPrimenodes.begin();
    while(it != vPrimenodes.end()){
        if((*it).vin == vin){
            LogPrint("primenode", "CPrimenodeMan: Removing Primenode %s - %i now\n", (*it).addr.ToString().c_str(), size() - 1);
            vPrimenodes.erase(it);
            break;
        } else {
            ++it;
        }
    }
}

std::string CPrimenodeMan::ToString() const
{
    std::ostringstream info;

    info << "primenodes: " << (int)vPrimenodes.size() <<
            ", peers who asked us for primenode list: " << (int)mAskedUsForPrimenodeList.size() <<
            ", peers we asked for primenode list: " << (int)mWeAskedForPrimenodeList.size() <<
            ", entries in Primenode list we asked for: " << (int)mWeAskedForPrimenodeListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
