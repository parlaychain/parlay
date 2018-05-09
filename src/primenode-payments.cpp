// Copyright (c) 2014-2015 The Dash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primenode-payments.h"
#include "primenodeman.h"
#include "darksend.h"
#include "util.h"
#include "sync.h"
#include "spork.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>

CCriticalSection cs_primenodepayments;

/** Object for who's going to get paid on which blocks */
CPrimenodePayments primenodePayments;
// keep track of Primenode votes I've seen
map<uint256, CPrimenodePaymentWinner> mapSeenPrimenodeVotes;

int CPrimenodePayments::GetMinPrimenodePaymentsProto() {
    return MIN_PRIMENODE_PAYMENT_PROTO_VERSION_1;
}

void ProcessMessagePrimenodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(!darkSendPool.IsBlockchainSynced()) return;

    if (strCommand == "mnget") { //Primenode Payments Request Sync

        if(pfrom->HasFulfilledRequest("mnget")) {
            LogPrintf("mnget - peer already asked me for the list\n");
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        pfrom->FulfilledRequest("mnget");
        primenodePayments.Sync(pfrom);
        LogPrintf("mnget - Sent Primenode winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "mnw") { //Primenode Payments Declare Winner

        LOCK(cs_primenodepayments);

        //this is required in litemode
        CPrimenodePaymentWinner winner;
        vRecv >> winner;

        if(pindexBest == NULL) return;

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CParlayAddress address2(address1);

        uint256 hash = winner.GetHash();
        if(mapSeenPrimenodeVotes.count(hash)) {
            if(fDebug) LogPrintf("mnw - seen vote %s Addr %s Height %d bestHeight %d\n", hash.ToString().c_str(), address2.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);
            return;
        }

        if(winner.nBlockHeight < pindexBest->nHeight - 10 || winner.nBlockHeight > pindexBest->nHeight+20){
            LogPrintf("mnw - winner out of range %s Addr %s Height %d bestHeight %d\n", winner.vin.ToString().c_str(), address2.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);
            return;
        }

        if(winner.vin.nSequence != std::numeric_limits<unsigned int>::max()){
            LogPrintf("mnw - invalid nSequence\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        LogPrintf("mnw - winning vote - Vin %s Addr %s Height %d bestHeight %d\n", winner.vin.ToString().c_str(), address2.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);

        if(!primenodePayments.CheckSignature(winner)){
            LogPrintf("mnw - invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSeenPrimenodeVotes.insert(make_pair(hash, winner));

        if(primenodePayments.AddWinningPrimenode(winner)){
            primenodePayments.Relay(winner);
        }
    }
}


bool CPrimenodePayments::CheckSignature(CPrimenodePaymentWinner& winner)
{
    //note: need to investigate why this is failing
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();
    std::string strPubKey = strMainPubKey ;
    CPubKey pubkey(ParseHex(strPubKey));

    std::string errorMessage = "";
    if(!darkSendSigner.VerifyMessage(pubkey, winner.vchSig, strMessage, errorMessage)){
        return false;
    }

    return true;
}

bool CPrimenodePayments::Sign(CPrimenodePaymentWinner& winner)
{
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!darkSendSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2))
    {
        LogPrintf("CPrimenodePayments::Sign - ERROR: Invalid Primenodeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, winner.vchSig, key2)) {
        LogPrintf("CPrimenodePayments::Sign - Sign message failed");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubkey2, winner.vchSig, strMessage, errorMessage)) {
        LogPrintf("CPrimenodePayments::Sign - Verify message failed");
        return false;
    }

    return true;
}

uint64_t CPrimenodePayments::CalculateScore(uint256 blockHash, CTxIn& vin)
{
    uint256 n1 = blockHash;
    uint256 n2 = Hash(BEGIN(n1), END(n1));
    uint256 n3 = Hash(BEGIN(vin.prevout.hash), END(vin.prevout.hash));
    uint256 n4 = n3 > n2 ? (n3 - n2) : (n2 - n3);

    //printf(" -- CPrimenodePayments CalculateScore() n2 = %d \n", n2.Get64());
    //printf(" -- CPrimenodePayments CalculateScore() n3 = %d \n", n3.Get64());
    //printf(" -- CPrimenodePayments CalculateScore() n4 = %d \n", n4.Get64());

    return n4.Get64();
}

bool CPrimenodePayments::GetBlockPayee(int nBlockHeight, CScript& payee, CTxIn& vin)
{
    BOOST_FOREACH(CPrimenodePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == nBlockHeight) {
            payee = winner.payee;
            vin = winner.vin;
            return true;
        }
    }

    return false;
}

bool CPrimenodePayments::GetWinningPrimenode(int nBlockHeight, CTxIn& vinOut)
{
    BOOST_FOREACH(CPrimenodePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == nBlockHeight) {
            vinOut = winner.vin;
            return true;
        }
    }

    return false;
}

bool CPrimenodePayments::AddWinningPrimenode(CPrimenodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-576)) {
        return false;
    }

    winnerIn.score = CalculateScore(blockHash, winnerIn.vin);

    bool foundBlock = false;
    BOOST_FOREACH(CPrimenodePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == winnerIn.nBlockHeight) {
            foundBlock = true;
            if(winner.score < winnerIn.score){
                winner.score = winnerIn.score;
                winner.vin = winnerIn.vin;
                winner.payee = winnerIn.payee;
                winner.vchSig = winnerIn.vchSig;

                mapSeenPrimenodeVotes.insert(make_pair(winnerIn.GetHash(), winnerIn));

                return true;
            }
        }
    }

    // if it's not in the vector
    if(!foundBlock){
        vWinning.push_back(winnerIn);
        mapSeenPrimenodeVotes.insert(make_pair(winnerIn.GetHash(), winnerIn));

        return true;
    }

    return false;
}

void CPrimenodePayments::CleanPaymentList()
{
    LOCK(cs_primenodepayments);

    if(pindexBest == NULL) return;

    int nLimit = std::max(((int)mnodeman.size())*((int)1.25), 1000);

    vector<CPrimenodePaymentWinner>::iterator it;
    for(it=vWinning.begin();it<vWinning.end();it++){
        if(pindexBest->nHeight - (*it).nBlockHeight > nLimit){
            if(fDebug) LogPrintf("CPrimenodePayments::CleanPaymentList - Removing old Primenode payment - block %d\n", (*it).nBlockHeight);
            vWinning.erase(it);
            break;
        }
    }
}

bool CPrimenodePayments::ProcessBlock(int nBlockHeight)
{
    LOCK(cs_primenodepayments);

    if(nBlockHeight <= nLastBlockHeight) return false;
    if(!enabled) return false;
    CPrimenodePaymentWinner newWinner;
    int nMinimumAge = mnodeman.CountEnabled();
    CScript payeeSource;

    uint256 hash;
    if(!GetBlockHash(hash, nBlockHeight-10)) return false;
    unsigned int nHash;
    memcpy(&nHash, &hash, 2);

    LogPrintf(" ProcessBlock Start nHeight %d - vin %s. \n", nBlockHeight, activePrimenode.vin.ToString().c_str());

    std::vector<CTxIn> vecLastPayments;
    BOOST_REVERSE_FOREACH(CPrimenodePaymentWinner& winner, vWinning)
    {
        //if we already have the same vin - we have one full payment cycle, break
        if(vecLastPayments.size() > (unsigned int)nMinimumAge) break;
        vecLastPayments.push_back(winner.vin);
    }

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    CPrimenode *pmn = mnodeman.FindOldestNotInVec(vecLastPayments, nMinimumAge);
    if(pmn != NULL)
    {
        LogPrintf(" Found by FindOldestNotInVec \n");

        newWinner.score = 0;
        newWinner.nBlockHeight = nBlockHeight;
        newWinner.vin = pmn->vin;

        if(pmn->rewardPercentage > 0 && (nHash % 100) <= (unsigned int)pmn->rewardPercentage) {
            newWinner.payee = pmn->rewardAddress;
        } else {
            newWinner.payee = GetScriptForDestination(pmn->pubkey.GetID());
        }

        payeeSource = GetScriptForDestination(pmn->pubkey.GetID());
    }

    //if we can't find new MN to get paid, pick first active MN counting back from the end of vecLastPayments list
    if(newWinner.nBlockHeight == 0 && nMinimumAge > 0)
    {
        LogPrintf(" Find by reverse \n");

        BOOST_REVERSE_FOREACH(CTxIn& vinLP, vecLastPayments)
        {
            CPrimenode* pmn = mnodeman.Find(vinLP);
            if(pmn != NULL)
            {
                pmn->Check();
                if(!pmn->IsEnabled()) continue;

                newWinner.score = 0;
                newWinner.nBlockHeight = nBlockHeight;
                newWinner.vin = pmn->vin;

                if(pmn->rewardPercentage > 0 && (nHash % 100) <= (unsigned int)pmn->rewardPercentage) {
                    newWinner.payee = pmn->rewardAddress;
                } else {
                    newWinner.payee = GetScriptForDestination(pmn->pubkey.GetID());
                }

                payeeSource = GetScriptForDestination(pmn->pubkey.GetID());

                break; // we found active MN
            }
        }
    }

    if(newWinner.nBlockHeight == 0) return false;

    CTxDestination address1;
    ExtractDestination(newWinner.payee, address1);
    CParlayAddress address2(address1);

    CTxDestination address3;
    ExtractDestination(payeeSource, address3);
    CParlayAddress address4(address3);

    LogPrintf("Winner payee %s nHeight %d vin source %s. \n", address2.ToString().c_str(), newWinner.nBlockHeight, address4.ToString().c_str());

    if(Sign(newWinner))
    {
        if(AddWinningPrimenode(newWinner))
        {
            Relay(newWinner);
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}


void CPrimenodePayments::Relay(CPrimenodePaymentWinner& winner)
{
    CInv inv(MSG_PRIMENODE_WINNER, winner.GetHash());

    vector<CInv> vInv;
    vInv.push_back(inv);
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        pnode->PushMessage("inv", vInv);
    }
}

void CPrimenodePayments::Sync(CNode* node)
{
    LOCK(cs_primenodepayments);

    BOOST_FOREACH(CPrimenodePaymentWinner& winner, vWinning)
        if(winner.nBlockHeight >= pindexBest->nHeight-10 && winner.nBlockHeight <= pindexBest->nHeight + 20)
            node->PushMessage("mnw", winner);
}


bool CPrimenodePayments::SetPrivKey(std::string strPrivKey)
{
    CPrimenodePaymentWinner winner;

    // Test signing successful, proceed
    strMasterPrivKey = strPrivKey;

    Sign(winner);

    if(CheckSignature(winner)){
        LogPrintf("CPrimenodePayments::SetPrivKey - Successfully initialized as Primenode payments master\n");
        enabled = true;
        return true;
    } else {
        return false;
    }
}
