// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The DarkCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEPRIMENODE_H
#define ACTIVEPRIMENODE_H

#include "uint256.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "primenode.h"
#include "main.h"
#include "init.h"
#include "wallet.h"
#include "darksend.h"

// Responsible for activating the primenode and pinging the network
class CActivePrimenode
{
public:
	// Initialized by init.cpp
	// Keys for the main primenode
	CPubKey pubKeyPrimenode;

	// Initialized while registering primenode
	CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActivePrimenode()
    {        
        status = PRIMENODE_NOT_PROCESSED;
    }

    void ManageStatus(); // manage status of main primenode

    bool Dseep(std::string& errorMessage); // ping for main primenode
    bool Dseep(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string &retErrorMessage, bool stop); // ping for any primenode

    bool StopPrimeNode(std::string& errorMessage); // stop main primenode
    bool StopPrimeNode(std::string strService, std::string strKeyPrimenode, std::string& errorMessage); // stop remote primenode
    bool StopPrimeNode(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string& errorMessage); // stop any primenode

    /// Register remote Primenode
    bool Register(std::string strService, std::string strKey, std::string txHash, std::string strOutputIndex, std::string strRewardAddress, std::string strRewardPercentage, std::string& errorMessage); 
    /// Register any Primenode
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyPrimenode, CPubKey pubKeyPrimenode, CScript rewardAddress, int rewardPercentage, std::string &retErrorMessage);  

    // get 500 PAR input that can be used for the primenode
    bool GetPrimeNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    bool GetPrimeNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetPrimeNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    bool GetPrimeNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    vector<COutput> SelectCoinsPrimenode();
    vector<COutput> SelectCoinsPrimenodeForPubKey(std::string collateralAddress);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

    // enable hot wallet mode (run a primenode with no funds)
    bool EnableHotColdPrimeNode(CTxIn& vin, CService& addr);
};

#endif
