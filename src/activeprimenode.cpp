// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "protocol.h"
#include "activeprimenode.h"
#include "primenodeman.h"
#include <boost/lexical_cast.hpp>
#include "clientversion.h"

//
// Bootup the primenode, look for a 500 PAR input and register on the network
//
void CActivePrimenode::ManageStatus()
{
    std::string errorMessage;

    if (fDebug) LogPrintf("CActivePrimenode::ManageStatus() - Begin\n");

    if(!fPrimeNode) return;

    //need correct adjusted time to send ping
    bool fIsInitialDownload = IsInitialBlockDownload();
    if(fIsInitialDownload) {
        status = PRIMENODE_SYNC_IN_PROCESS;
        LogPrintf("CActivePrimenode::ManageStatus() - Sync in progress. Must wait until sync is complete to start primenode.\n");
        return;
    }

    if(status == PRIMENODE_INPUT_TOO_NEW || status == PRIMENODE_NOT_CAPABLE || status == PRIMENODE_SYNC_IN_PROCESS){
        status = PRIMENODE_NOT_PROCESSED;
    }

    if(status == PRIMENODE_NOT_PROCESSED) {
        if(strPrimeNodeAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the primenodeaddr configuration option.";
                status = PRIMENODE_NOT_CAPABLE;
                LogPrintf("CActivePrimenode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }
        } else {
        	service = CService(strPrimeNodeAddr, true);
        }

        LogPrintf("CActivePrimenode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString().c_str());

                  
            if(!ConnectNode((CAddress)service, service.ToString().c_str())){
                notCapableReason = "Could not connect to " + service.ToString();
                status = PRIMENODE_NOT_CAPABLE;
                LogPrintf("CActivePrimenode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }
        

        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            status = PRIMENODE_NOT_CAPABLE;
            LogPrintf("CActivePrimenode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
            return;
        }

        // Set defaults
        status = PRIMENODE_NOT_CAPABLE;
        notCapableReason = "Unknown. Check debug.log for more information.\n";

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetPrimeNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            if(GetInputAge(vin) < PRIMENODE_MIN_CONFIRMATIONS){
                notCapableReason = "Input must have least " + boost::lexical_cast<string>(PRIMENODE_MIN_CONFIRMATIONS) +
                        " confirmations - " + boost::lexical_cast<string>(GetInputAge(vin)) + " confirmations";
                LogPrintf("CActivePrimenode::ManageStatus() - %s\n", notCapableReason.c_str());
                status = PRIMENODE_INPUT_TOO_NEW;
                return;
            }

            LogPrintf("CActivePrimenode::ManageStatus() - Is capable prime node!\n");

            status = PRIMENODE_IS_CAPABLE;
            notCapableReason = "";

            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyPrimenode;
            CKey keyPrimenode;

            if(!darkSendSigner.SetKey(strPrimeNodePrivKey, errorMessage, keyPrimenode, pubKeyPrimenode))
            {
            	LogPrintf("ActivePrimenode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
            	return;
            }

            /* rewards are not supported in Parlay.conf */
            CScript rewardAddress = CScript();
            int rewardPercentage = 0;

            if(!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyPrimenode, pubKeyPrimenode, rewardAddress, rewardPercentage, errorMessage)) {
                LogPrintf("CActivePrimenode::ManageStatus() - Error on Register: %s\n", errorMessage.c_str());
            }

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
        	LogPrintf("CActivePrimenode::ManageStatus() - Could not find suitable coins!\n");
        }
    }

    //send to all peers
    if(!Dseep(errorMessage)) {
    	LogPrintf("CActivePrimenode::ManageStatus() - Error on Ping: %s\n", errorMessage.c_str());
    }
}

// Send stop dseep to network for remote primenode
bool CActivePrimenode::StopPrimeNode(std::string strService, std::string strKeyPrimenode, std::string& errorMessage) {
	CTxIn vin;
    CKey keyPrimenode;
    CPubKey pubKeyPrimenode;

    if(!darkSendSigner.SetKey(strKeyPrimenode, errorMessage, keyPrimenode, pubKeyPrimenode)) {
    	LogPrintf("CActivePrimenode::StopPrimeNode() - Error: %s\n", errorMessage.c_str());
		return false;
	}

    if (GetPrimeNodeVin(vin, pubKeyPrimenode, keyPrimenode)){
        LogPrintf("PrimenodeStop::VinFound: %s\n", vin.ToString());
    }

	return StopPrimeNode(vin, CService(strService, true), keyPrimenode, pubKeyPrimenode, errorMessage);
}

// Send stop dseep to network for main primenode
bool CActivePrimenode::StopPrimeNode(std::string& errorMessage) {
	if(status != PRIMENODE_IS_CAPABLE && status != PRIMENODE_REMOTELY_ENABLED) {
		errorMessage = "primenode is not in a running status";
    	LogPrintf("CActivePrimenode::StopPrimeNode() - Error: %s\n", errorMessage.c_str());
		return false;
	}

	status = PRIMENODE_STOPPED;

    CPubKey pubKeyPrimenode;
    CKey keyPrimenode;

    if(!darkSendSigner.SetKey(strPrimeNodePrivKey, errorMessage, keyPrimenode, pubKeyPrimenode))
    {
    	LogPrintf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
    	return false;
    }

	return StopPrimeNode(vin, service, keyPrimenode, pubKeyPrimenode, errorMessage);
}

// Send stop dseep to network for any primenode
bool CActivePrimenode::StopPrimeNode(CTxIn vin, CService service, CKey keyPrimenode, CPubKey pubKeyPrimenode, std::string& errorMessage) {
   	pwalletMain->UnlockCoin(vin.prevout);
	return Dseep(vin, service, keyPrimenode, pubKeyPrimenode, errorMessage, true);
}

bool CActivePrimenode::Dseep(std::string& errorMessage) {
	if(status != PRIMENODE_IS_CAPABLE && status != PRIMENODE_REMOTELY_ENABLED) {
		errorMessage = "primenode is not in a running status";
    	LogPrintf("CActivePrimenode::Dseep() - Error: %s\n", errorMessage.c_str());
		return false;
	}

    CPubKey pubKeyPrimenode;
    CKey keyPrimenode;

    if(!darkSendSigner.SetKey(strPrimeNodePrivKey, errorMessage, keyPrimenode, pubKeyPrimenode))
    {
    	LogPrintf("CActivePrimenode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
    	return false;
    }

	return Dseep(vin, service, keyPrimenode, pubKeyPrimenode, errorMessage, false);
}

bool CActivePrimenode::Dseep(CTxIn vin, CService service, CKey keyPrimenode, CPubKey pubKeyPrimenode, std::string &retErrorMessage, bool stop) {
    std::string errorMessage;
    std::vector<unsigned char> vchPrimeNodeSignature;
    std::string strPrimeNodeSignMessage;
    int64_t primeNodeSignatureTime = GetAdjustedTime();

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(primeNodeSignatureTime) + boost::lexical_cast<std::string>(stop);

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchPrimeNodeSignature, keyPrimenode)) {
    	retErrorMessage = "sign message failed: " + errorMessage;
    	LogPrintf("CActivePrimenode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyPrimenode, vchPrimeNodeSignature, strMessage, errorMessage)) {
    	retErrorMessage = "Verify message failed: " + errorMessage;
    	LogPrintf("CActivePrimenode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    // Update Last Seen timestamp in primenode list
    CPrimenode* pmn = mnodeman.Find(vin);
    if(pmn != NULL)
    {
        if(stop)
            mnodeman.Remove(pmn->vin);
        else
            pmn->UpdateLastSeen();
    } else {
    	// Seems like we are trying to send a ping while the primenode is not registered in the network
    	retErrorMessage = "Darksend Primenode List doesn't include our primenode, Shutting down primenode pinging service! " + vin.ToString();
    	LogPrintf("CActivePrimenode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        status = PRIMENODE_NOT_CAPABLE;
        notCapableReason = retErrorMessage;
        return false;
    }

    //send to all peers
    LogPrintf("CActivePrimenode::Dseep() - RelayPrimenodeEntryPing vin = %s\n", vin.ToString().c_str());
    mnodeman.RelayPrimenodeEntryPing(vin, vchPrimeNodeSignature, primeNodeSignatureTime, stop);

    return true;
}

bool CActivePrimenode::Register(std::string strService, std::string strKeyPrimenode, std::string txHash, std::string strOutputIndex, std::string strRewardAddress, std::string strRewardPercentage, std::string& errorMessage) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyPrimenode;
    CKey keyPrimenode;
    CScript rewardAddress = CScript();
    int rewardPercentage = 0;

    if(!darkSendSigner.SetKey(strKeyPrimenode, errorMessage, keyPrimenode, pubKeyPrimenode))
    {
        LogPrintf("CActivePrimenode::Register() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    if(!GetPrimeNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, txHash, strOutputIndex)) {
        errorMessage = "could not allocate vin";
        LogPrintf("CActivePrimenode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }
    CParlayAddress address;
    if (strRewardAddress != "")
    {
        if(!address.SetString(strRewardAddress))
        {
            LogPrintf("ActivePrimenode::Register - Invalid Reward Address\n");
            return false;
        }
        rewardAddress.SetDestination(address.Get());

        try {
            rewardPercentage = boost::lexical_cast<int>( strRewardPercentage );
        } catch( boost::bad_lexical_cast const& ) {
            LogPrintf("ActivePrimenode::Register - Invalid Reward Percentage (Couldn't cast)\n");
            return false;
        }

        if(rewardPercentage < 0 || rewardPercentage > 100)
        {
            LogPrintf("ActivePrimenode::Register - Reward Percentage Out Of Range\n");
            return false;
        }
    }

	return Register(vin, CService(strService, true), keyCollateralAddress, pubKeyCollateralAddress, keyPrimenode, pubKeyPrimenode, rewardAddress, rewardPercentage, errorMessage);
}

bool CActivePrimenode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyPrimenode, CPubKey pubKeyPrimenode, CScript rewardAddress, int rewardPercentage, std::string &retErrorMessage) {
    std::string errorMessage;
    std::vector<unsigned char> vchPrimeNodeSignature;
    std::string strPrimeNodeSignMessage;
    int64_t primeNodeSignatureTime = GetAdjustedTime();

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyPrimenode.begin(), pubKeyPrimenode.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(primeNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION) + rewardAddress.ToString() + boost::lexical_cast<std::string>(rewardPercentage);

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchPrimeNodeSignature, keyCollateralAddress)) {
		retErrorMessage = "sign message failed: " + errorMessage;
		LogPrintf("CActivePrimenode::Register() - Error: %s\n", retErrorMessage.c_str());
		return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchPrimeNodeSignature, strMessage, errorMessage)) {
		retErrorMessage = "Verify message failed: " + errorMessage;
		LogPrintf("CActivePrimenode::Register() - Error: %s\n", retErrorMessage.c_str());
		return false;
	}

    CPrimenode* pmn = mnodeman.Find(vin);
    if(pmn == NULL)
    {
        LogPrintf("CActivePrimenode::Register() - Adding to primenode list service: %s - vin: %s\n", service.ToString().c_str(), vin.ToString().c_str());
        CPrimenode mn(service, vin, pubKeyCollateralAddress, vchPrimeNodeSignature, primeNodeSignatureTime, pubKeyPrimenode, PROTOCOL_VERSION, rewardAddress, rewardPercentage); 
        mn.ChangeNodeStatus(false);
        mn.UpdateLastSeen(primeNodeSignatureTime);
        mnodeman.Add(mn);
    }

    //send to all peers
    LogPrintf("CActivePrimenode::Register() - RelayElectionEntry vin = %s\n", vin.ToString().c_str());
    mnodeman.RelayPrimenodeEntry(vin, service, vchPrimeNodeSignature, primeNodeSignatureTime, pubKeyCollateralAddress, pubKeyPrimenode, -1, -1, primeNodeSignatureTime, PROTOCOL_VERSION, rewardAddress, rewardPercentage);

    return true;
}

bool CActivePrimenode::GetPrimeNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
	return GetPrimeNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActivePrimenode::GetPrimeNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsPrimenode();
    COutput *selectedOutput;

    // Find the vin
	if(!strTxHash.empty()) {
		// Let's find it
		uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		BOOST_FOREACH(COutput& out, possibleCoins) {
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				break;
			}
		}
		if(!found) {
			LogPrintf("CActivePrimenode::GetPrimeNodeVin - Could not locate valid vin\n");
			return false;
		}
	} else {
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0) {
			selectedOutput = &possibleCoins[0];
		} else {
			LogPrintf("CActivePrimenode::GetPrimeNodeVin - Could not locate specified vin from possible list\n");
			return false;
		}
    }

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

bool CActivePrimenode::GetPrimeNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
	return GetPrimeNodeVinForPubKey(collateralAddress, vin, pubkey, secretKey, "", "");
}

bool CActivePrimenode::GetPrimeNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsPrimenodeForPubKey(collateralAddress);
    COutput *selectedOutput;

    // Find the vin
	if(!strTxHash.empty()) {
		// Let's find it
		uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		BOOST_FOREACH(COutput& out, possibleCoins) {
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				break;
			}
		}
		if(!found) {
			LogPrintf("CActivePrimenode::GetPrimeNodeVinForPubKey - Could not locate valid vin\n");
			return false;
		}
	} else {
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0) {
			selectedOutput = &possibleCoins[0];
		} else {
			LogPrintf("CActivePrimenode::GetPrimeNodeVinForPubKey - Could not locate specified vin from possible list\n");
			return false;
		}
    }

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract primenode vin information from output
bool CActivePrimenode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {

    CScript pubScript;

	vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

	CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CParlayAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActivePrimenode::GetPrimeNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf ("CActivePrimenode::GetPrimeNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running primenode
vector<COutput> CActivePrimenode::SelectCoinsPrimenode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;

    // Retrieve all possible outputs
    pwalletMain->AvailableCoinsMN(vCoins);

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == GetMNCollateral(pindexBest->nHeight)*COIN) { //exactly
        	filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// get all possible outputs for running primenode for a specific pubkey
vector<COutput> CActivePrimenode::SelectCoinsPrimenodeForPubKey(std::string collateralAddress)
{
    CParlayAddress address(collateralAddress);
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].scriptPubKey == scriptPubKey && out.tx->vout[out.i].nValue == GetMNCollateral(pindexBest->nHeight)*COIN) { //exactly
        	filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a primenode, this can enable to run as a hot wallet with no funds
bool CActivePrimenode::EnableHotColdPrimeNode(CTxIn& newVin, CService& newService)
{
    if(!fPrimeNode) return false;

    status = PRIMENODE_REMOTELY_ENABLED;

    //The values below are needed for signing dseep messages going forward
    this->vin = newVin;
    this->service = newService;

    LogPrintf("CActivePrimenode::EnableHotColdPrimeNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
