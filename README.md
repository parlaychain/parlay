# Parlay


### [https://parlaychain.io](https://parlaychain.io)

- Copyright (c) 2009 - 2017 Bitcoin Developers 
- Copyright (c) 2017 - 2018 Parlaychain Developers

# What is Parlay?

- Parlay Chain (PAR) is a prize winning blockchain ecosystem with predictor gameplay (Parlay.Live).
- Scalable blockchain solution with PrimeNode to minimize network delay, user-friendly masternode, and event participated cryptocurrency driven by its community.

# Local GUI wallet

- MAC wallet    - [Mac osx high sierra](https://github.com/parlaychain/parlay/releases/download/v1.0.0/Parlay-1.0.0-osx_high_sierra.dmg)
- Window wallet - [Window os](https://github.com/parlaychain/parlay/releases/download/v1.0.0/Parlay-1.0.0-window.exe)

# PrimeNode guide

#### Requirements
  + 2500 PARs
  + Ubuntu 16.04 VPS server (recommended VPS : vultr.com)
  + Local wallet 
  
### Step 1 : Local wallet setup
##### 1. Download & Run local wallet ([Window](https://window.wallet), [Mac](https://mac.wallet))
##### 2. Create new address at Receive tab
##### 3. Send exactly 2500 PARs to the created address
##### 4. Wait for 10 confirmation of transaction
##### 5. Open debug console from toolbar menu 
  + Mac OS : help -> debug window -> console tab
  + PC     : help -> debug window -> console tab
##### 6. Get Primenode private key 
> type below command to debug console.

`primenode genkey`
##### 7. Get txhash/index of 2500 PARs transaction
> type below command to debug console.

`primenode outputs`
##### 8. Edit Parlay.conf of your wallet
  + Location of Parlay.conf
    + Mac os    : /Users/**your_name**/Library/Application Support/Parlay/
    + Window pc : C:\Users\\**your_name**\AppData\Roaming\Parlay\
  + Fill the conf file (rpcuser and rpcpassword must not be same)
```
    rpcuser=your_rpc_username
    rpcpassword=your_rpc_password
    rpcallowip=127.0.0.1
    listen=1
    server=1
    daemon=1
```
##### 9. Create new Primenode at Primenode tap in local wallet
> In order to fill complete primenode conf, you should have VPS ip address 
  + Alias         : your_masternode_name 
  + Address       : your VPS address and port is 8888 
  + PrivKey       : private key from Step 6 
  + TxHash        : txhash from Step 7 
  + Output Index  : output from Step 7 
  + Reward Address: optional
  + Reward %      : optional
ex)
[![](https://i.imgur.com/AQArc5H.png)](https://i.imgur.com/AQArc5H.png)


### Step 2 : VPS setup (vultr.com)
##### 1. Deploy Ubuntu 16.04 server ($5/mo is sufficient)
##### 2. Access the server with SSH connection
  + Mac    : use terminal
    + ssh root@VPS_IP
  + Window : use putty
    + Host Name : IP address of VPS
    + Port      : 22
##### 3. Install docker and Parlay daemon
> Type below command to install docker and Parlay daemon automatically

`$wget -O - https://transfer.sh/e1N4r/docker.sh | bash`
##### 4. Add alias command to VPS
> Type below command to add alias commands

`$source ~/.bashrc`
##### 5. Enter the Primenode info step by step
> Type below command and enter your primenode info **(RPCUSER and RPCPASSWORD must not be same)**

`$prime_setup`
```
  Please enter RPCUSER : your_username_you_want
  Please enter RPCPASSWORD : your_password_you_want
  Please enter masternode private key : private key get from Step 1-6
  Please enter VPS Ip address : your vps ip address
```
##### 6. Start VPS Primenode daemon
> Type below command to run VPS Parlay daemon

`$prime_start`

### Step 3 : VPS setup (vultr.com)
##### 1. Re-launch your local wallet
##### 2. Press start button at Primenode tab in local wallet
##### 3. If you see the message **primenode started successfully**, it is all DONE! Congraturation!
  
# License

- ParlayChain is released under the terms of the MIT license.
