// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include <deque>
#include <boost/array.hpp>
#include <boost/foreach.hpp>
#include <openssl/rand.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include "btcNode/MessageHeader.h"
#include "btcNode/Inventory.h"
#include "btcNode/Endpoint.h"
#include "btcNode/EndpointPool.h"
#include "btcNode/main.h"
#include "btcNode/Filter.h"
#include "btcNode/MessageParser.h"

class CAddrDB;
class CRequestTracker;
class Peer;
class CBlockIndex;

extern int nConnectTimeout;

class MessageHandler;
extern MessageHandler* _msgHandler;



inline unsigned int ReceiveBufferSize() { return 1000*GetArg("-maxreceivebuffer", 10*1000); }
inline unsigned int SendBufferSize() { return 1000*GetArg("-maxsendbuffer", 10*1000); }
static const unsigned int PUBLISH_HOPS = 5;

bool ConnectSocket(const Endpoint& addrConnect, SOCKET& hSocketRet, int nTimeout=nConnectTimeout);
bool Lookup(const char *pszName, std::vector<Endpoint>& vaddr, int nServices, int nMaxSolutions, bool fAllowLookup = false, int portDefault = 0, bool fAllowPort = false);
bool Lookup(const char *pszName, Endpoint& addr, int nServices, bool fAllowLookup = false, int portDefault = 0, bool fAllowPort = false);
bool GetMyExternalIP(unsigned int& ipRet);
//bool AddAddress(Endpoint addr, int64 nTimePenalty=0, CAddrDB *pAddrDB=NULL);
//void AddressCurrentlyConnected(const Endpoint& addr);
Peer* FindNode(unsigned int ip);
Peer* ConnectNode(Endpoint addrConnect, int64 nTimeout=0);
bool AnySubscribed(unsigned int nChannel);
void MapPort(bool fMapPort);
void DNSAddressSeed();
bool BindListenPort(std::string& strError=REF(std::string()));
void StartNode(void* parg);
bool StopNode();




extern bool fClient;
extern bool fAllowDNS;
extern uint64 nLocalServices;
//extern Endpoint __localhost;
extern uint64 nLocalHostNonce;
extern boost::array<int, 10> vnThreadsRunning;

extern std::vector<Peer*> vNodes;
extern CCriticalSection cs_vNodes;
//extern std::map<std::vector<unsigned char>, Endpoint> mapAddresses;
//extern CCriticalSection cs_mapAddresses;

extern EndpointPool* __endpointPool;


extern std::map<Inventory, CDataStream> mapRelay;
extern std::deque<std::pair<int64, Inventory> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;
extern std::map<Inventory, int64> mapAlreadyAskedFor;

// Settings
extern int fUseProxy;
extern Endpoint addrProxy;




#ifndef _LIBBTC_ASIO_

class CRequestTracker
{
public:
    void (*fn)(void*, CDataStream&);
    void* param1;
    
    explicit CRequestTracker(void (*fnIn)(void*, CDataStream&)=NULL, void* param1In=NULL)
    {
    fn = fnIn;
    param1 = param1In;
    }
    
    bool IsNull()
    {
    return fn == NULL;
    }
};

class Peer
{
public:
    // socket
    uint64 nServices;
    SOCKET hSocket;
    CDataStream vSend;
    CDataStream vRecv;
    CCriticalSection cs_vSend;
    CCriticalSection cs_vRecv;
    int64 nLastSend;
    int64 nLastRecv;
    int64 nLastSendEmpty;
    int64 nTimeConnected;
    unsigned int nHeaderStart;
    unsigned int nMessageStart;
    Endpoint addr;
    int nVersion;
    std::string strSubVer;
    bool fClient;
    bool fInbound;
    bool fNetworkNode;
    bool fSuccessfullyConnected;
    bool fDisconnect;
protected:
    int nRefCount;
public:
    int64 nReleaseTime;
    std::map<uint256, CRequestTracker> mapRequests;
    CCriticalSection cs_mapRequests;
    uint256 hashContinue;
    CBlockLocator locatorLastGetBlocksBegin;
    uint256 hashLastGetBlocksEnd;
    int nStartingHeight;

    // flood relay
    std::vector<Endpoint> vAddrToSend;
    std::set<Endpoint> setAddrKnown;
    bool fGetAddr;
    std::set<uint256> setKnown;

    // inventory based relay
    std::set<Inventory> setInventoryKnown;
    std::vector<Inventory> vInventoryToSend;
    CCriticalSection cs_inventory;
    std::multimap<int64, Inventory> mapAskFor;

    // publish and subscription
    std::vector<char> vfSubscribe;

    Peer(SOCKET hSocketIn, Endpoint addrIn, bool fInboundIn=false) : _message()
    {
        nServices = 0;
        hSocket = hSocketIn;
        vSend.SetType(SER_NETWORK);
        vSend.SetVersion(0);
        vRecv.SetType(SER_NETWORK);
        vRecv.SetVersion(0);
        // Version 0.2 obsoletes 20 Feb 2012
        if (GetTime() > 1329696000)
        {
            vSend.SetVersion(209);
            vRecv.SetVersion(209);
        }
        nLastSend = 0;
        nLastRecv = 0;
        nLastSendEmpty = GetTime();
        nTimeConnected = GetTime();
        nHeaderStart = -1;
        nMessageStart = -1;
        addr = addrIn;
        nVersion = 0;
        strSubVer = "";
        fClient = false; // set by version message
        fInbound = fInboundIn;
        fNetworkNode = false;
        fSuccessfullyConnected = false;
        fDisconnect = false;
        nRefCount = 0;
        nReleaseTime = 0;
        hashContinue = 0;
        locatorLastGetBlocksBegin.SetNull();
        hashLastGetBlocksEnd = 0;
        nStartingHeight = -1;
        fGetAddr = false;
        vfSubscribe.assign(256, false);

        // Be shy and don't send version until we hear
        if (!fInbound)
            PushVersion();
    }

    ~Peer()
    {
        if (hSocket != INVALID_SOCKET)
        {
            closesocket(hSocket);
            hSocket = INVALID_SOCKET;
        }
    }

private:
    Peer(const Peer&);
    void operator=(const Peer&);
public:


    int GetRefCount()
    {
        return std::max(nRefCount, 0) + (GetTime() < nReleaseTime ? 1 : 0);
    }

    Peer* AddRef(int64 nTimeout=0)
    {
        if (nTimeout != 0)
            nReleaseTime = std::max(nReleaseTime, GetTime() + nTimeout);
        else
            nRefCount++;
        return this;
    }

    void Release()
    {
        nRefCount--;
    }



    void AddAddressKnown(const Endpoint& addr)
    {
        setAddrKnown.insert(addr);
    }

    void PushAddress(const Endpoint& addr)
    {
        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (addr.isValid() && !setAddrKnown.count(addr))
            vAddrToSend.push_back(addr);
    }


    void AddInventoryKnown(const Inventory& inv)
    {
        CRITICAL_BLOCK(cs_inventory)
            setInventoryKnown.insert(inv);
    }

    void PushInventory(const Inventory& inv)
    {
        CRITICAL_BLOCK(cs_inventory)
            if (!setInventoryKnown.count(inv))
                vInventoryToSend.push_back(inv);
    }

    void AskFor(const Inventory& inv)
    {
        // We're using mapAskFor as a priority queue,
        // the key is the earliest time the request can be sent
        int64& nRequestTime = mapAlreadyAskedFor[inv];
        printf("askfor %s   %"PRI64d"\n", inv.toString().c_str(), nRequestTime);

        // Make sure not to reuse time indexes to keep things in the same order
        int64 nNow = (GetTime() - 1) * 1000000;
        static int64 nLastTime;
        nLastTime = nNow = std::max(nNow, ++nLastTime);

        // Each retry is 2 minutes after the last
        nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
        mapAskFor.insert(std::make_pair(nRequestTime, inv));
    }



    void BeginMessage(const char* pszCommand)
    {
        cs_vSend.Enter("cs_vSend", __FILE__, __LINE__);
        if (nHeaderStart != -1)
            AbortMessage();
        nHeaderStart = vSend.size();
        vSend << MessageHeader(pszCommand, 0);
        nMessageStart = vSend.size();
        if (fDebug)
            printf("%s ", DateTimeStrFormat("%x %H:%M:%S", GetTime()).c_str());
        printf("sending: %s ", pszCommand);
    }

    void AbortMessage()
    {
        if (nHeaderStart == -1)
            return;
        vSend.resize(nHeaderStart);
        nHeaderStart = -1;
        nMessageStart = -1;
        cs_vSend.Leave();
        printf("(aborted)\n");
    }

    void EndMessage()
    {
        if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
        {
            printf("dropmessages DROPPING SEND MESSAGE\n");
            AbortMessage();
            return;
        }

        if (nHeaderStart == -1)
            return;

        // Set the size
        unsigned int nSize = vSend.size() - nMessageStart;
        memcpy((char*)&vSend[nHeaderStart] + offsetof(MessageHeader, nMessageSize), &nSize, sizeof(nSize));

        // Set the checksum
        if (vSend.GetVersion() >= 209)
        {
            uint256 hash = Hash(vSend.begin() + nMessageStart, vSend.end());
            unsigned int nChecksum = 0;
            memcpy(&nChecksum, &hash, sizeof(nChecksum));
            assert(nMessageStart - nHeaderStart >= offsetof(MessageHeader, nChecksum) + sizeof(nChecksum));
            memcpy((char*)&vSend[nHeaderStart] + offsetof(MessageHeader, nChecksum), &nChecksum, sizeof(nChecksum));
        }

        printf("(%d bytes) ", nSize);
        printf("\n");

        nHeaderStart = -1;
        nMessageStart = -1;
        cs_vSend.Leave();
    }

    void EndMessageAbortIfEmpty()
    {
        if (nHeaderStart == -1)
            return;
        int nSize = vSend.size() - nMessageStart;
        if (nSize > 0)
            EndMessage();
        else
            AbortMessage();
    }



    void PushVersion()
    {
        /// when NTP implemented, change to just nTime = GetAdjustedTime()
        int64 nTime = (fInbound ? GetAdjustedTime() : GetTime());
        Endpoint addrYou = (fUseProxy ? Endpoint("0.0.0.0") : addr);
        Endpoint addrMe = (fUseProxy ? Endpoint("0.0.0.0") : _endpointPool->getLocal());
        RAND_bytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
        PushMessage("version", VERSION, nLocalServices, nTime, addrYou, addrMe,
                    nLocalHostNonce, std::string(pszSubVer), __blockChain->getBestHeight());
    }




    void PushMessage(const char* pszCommand)
    {
        try
        {
            BeginMessage(pszCommand);
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1>
    void PushMessage(const char* pszCommand, const T1& a1)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4 << a5;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4 << a5 << a6;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4 << a5 << a6 << a7;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9)
    {
        try
        {
            BeginMessage(pszCommand);
            vSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }


    void PushRequest(const char* pszCommand,
                     void (*fn)(void*, CDataStream&), void* param1)
    {
        uint256 hashReply;
        RAND_bytes((unsigned char*)&hashReply, sizeof(hashReply));

        CRITICAL_BLOCK(cs_mapRequests)
            mapRequests[hashReply] = CRequestTracker(fn, param1);

        PushMessage(pszCommand, hashReply);
    }

    template<typename T1>
    void PushRequest(const char* pszCommand, const T1& a1,
                     void (*fn)(void*, CDataStream&), void* param1)
    {
        uint256 hashReply;
        RAND_bytes((unsigned char*)&hashReply, sizeof(hashReply));

        CRITICAL_BLOCK(cs_mapRequests)
            mapRequests[hashReply] = CRequestTracker(fn, param1);

        PushMessage(pszCommand, hashReply, a1);
    }

    template<typename T1, typename T2>
    void PushRequest(const char* pszCommand, const T1& a1, const T2& a2,
                     void (*fn)(void*, CDataStream&), void* param1)
    {
        uint256 hashReply;
        RAND_bytes((unsigned char*)&hashReply, sizeof(hashReply));

        CRITICAL_BLOCK(cs_mapRequests)
            mapRequests[hashReply] = CRequestTracker(fn, param1);

        PushMessage(pszCommand, hashReply, a1, a2);
    }

    bool ProcessMessage(std::string strCommand, CDataStream& vRecv);
    bool ProcessMessages();
    bool SendMessages(bool fSendTrickle);    


    void PushGetBlocks(const CBlockLocator locatorBegin, uint256 hashEnd);

    void CloseSocketDisconnect();
private:
    Message _message;
    MessageParser _msgParser;
};


inline void RelayInventory(const Inventory& inv)
{
    // Put on lists to offer to the other nodes
    CRITICAL_BLOCK(cs_vNodes)
        BOOST_FOREACH(Peer* pnode, vNodes)
            pnode->PushInventory(inv);
}

template<typename T>
void RelayMessage(const Inventory& inv, const T& a)
{
    CDataStream ss(SER_NETWORK);
    ss.reserve(10000);
    ss << a;
    RelayMessage(inv, ss);
}

template<>
inline void RelayMessage<>(const Inventory& inv, const CDataStream& ss)
{
    CRITICAL_BLOCK(cs_mapRelay)
    {
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay[inv] = ss;
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }

    RelayInventory(inv);
}
#endif // _LIBBTC_ASIO_



#endif
