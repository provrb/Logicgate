#include "../Headers/server.h"
#ifdef SERVER_RELEASE

RSAKeys ServerInterface::GenerateRSAPair() {
	return std::make_pair("empty", "empty");
}


void ServerInterface::ListenForUDPMessages() {

	// UDP requests are not encrypted.
	sockaddr_in recvAddr;
	int serverAddrSize = sizeof(this->UDPServerDetails.addr);

	// receive while udp server is alive
	while ( this->UDPServerDetails.alive ) {
		ClientMessage recvBuffer;

		// receive data from udp messages
		int receive = ReceiveFrom(this->UDPServerDetails.sfd,
			reinterpret_cast< char* >( &recvBuffer ),
			1024, 0, reinterpret_cast< sockaddr* >( &this->UDPServerDetails.addr ),
			&serverAddrSize
		);

		if ( receive == SOCKET_ERROR )
			continue;

		PerformUDPRequest(recvBuffer);
	}
}


void ServerInterface::TCPReceiveMessagesFromClient(long cuid) {
}

void ServerInterface::AcceptTCPConnections() {
	while ( this->ClientList.size() < MAX_CON )
	{
		// accept
		sockaddr_in addr = {};
		int size = sizeof(sockaddr_in);
		SOCKET clientSocket = AcceptOnSocket(this->TCPServerDetails.sfd, reinterpret_cast<sockaddr*>( &addr ), &size);
		if ( clientSocket == INVALID_SOCKET )
			continue;

		Client newClient(addr); // make a new client and store the addr info in it
		newClient.SetRSAKeys(this->GenerateRSAPair());
		AddToClientList(newClient); // add them to the client list

		// start receiving tcp data from that client for the lifetime of that client
		std::thread receive(&ServerInterface::TCPReceiveMessagesFromClient, this, newClient.ClientUID);
		receive.detach();
	}
}

Server ServerInterface::NewServerInstance(SocketTypes serverType, int port) {
	Server server = {};
	
	if ( !NetCommon::WSAInitialized )
		NetCommon::LoadWSAFunctions();

	// create socket for server type
	// update server fields
	if ( serverType == TCP ) {
		server.sfd = CreateSocket(AF_INET, SOCK_STREAM, 0);
		if ( server.sfd == INVALID_SOCKET )
			return server;

		server.type = SOCK_STREAM;
	} else if ( serverType == UDP) {
		server.sfd = CreateSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if ( server.sfd == INVALID_SOCKET )
			return server;

		server.type = SOCK_DGRAM;
	}

	// update server fields
	server.addr.sin_family	    = AF_INET;
	server.addr.sin_addr.s_addr = INADDR_ANY; // servers shouldnt bind to any address
	server.addr.sin_port        = htons(port);
	server.port                 = port;

	return server;
}

BOOL ServerInterface::StartServer(Server& server) {
	// bind
	int status = SOCKET_ERROR;
	status = BindSocket(server.sfd, ( sockaddr* ) &server.addr, sizeof(server.addr));
	if ( status == SOCKET_ERROR )
		return FALSE;

	MarkServerAsAlive(server); // alive once binded

	// listen if TCP server
	if ( server.type == SOCK_STREAM ) {
		status = SocketListen(server.sfd, SOMAXCONN);
		if ( status == SOCKET_ERROR )
			return FALSE;

		this->TCPServerDetails = server;

		// start accepting
		std::thread acceptThread(&ServerInterface::AcceptTCPConnections, this);
		acceptThread.detach(); // run accept thread even after this function returns
	}
	// otherwise if not tcp server then listen for udp messaages
	else if ( server.type == SOCK_DGRAM ) {
		this->UDPServerDetails = server;

		std::thread receiveThread(&ServerInterface::ListenForUDPMessages, this);
		receiveThread.detach();
	}

	return TRUE;
}

BOOL ServerInterface::TCPSendMessageToClient(long cuid, ServerCommand& req) {
	return TRUE;
}

ClientResponse ServerInterface::WaitForClientResponse(long cuid) {
	return {};
}

BOOL ServerInterface::UDPSendMessageToClient(Client& client, UDPMessage& message) {
	
	if ( !client.SocketReady(UDP) )
		return FALSE;

	int result = SendTo(
		client.UDPSocket, reinterpret_cast< char* >( &message ),
		sizeof(message), 0,
		reinterpret_cast<sockaddr*>(&client.AddressInfo), sizeof(client.AddressInfo)
	);

	if ( result == SOCKET_ERROR )
		return FALSE;

	return TRUE;
}

BOOL ServerInterface::UDPSendMessageToClient(long cuid, UDPMessage& message) {
	ClientData data   = GetClientData(cuid);
	Client     client = data.first;

	return UDPSendMessageToClient(client, message);
}

BOOL ServerInterface::PerformUDPRequest(ClientMessage req) {
	BOOL success = FALSE;
	
	// udp isnt encrypted, which is why we want to get out of udp as fast as possible
	// only serialized as a bytestring to send over sockets
	if ( !req.valid )
		return FALSE;

	switch ( req.action ) {
	case ClientMessage::CONNECT_CLIENT:
		Client client = *reinterpret_cast< Client* >( req.client );
		
		// client wants to connect so respond with tcp server details
		UDPMessage response = {};
		response.TCPServer = GetTCPServerDetails();
		response.isValid = TRUE;
		if ( UDPSendMessageToClient(client, response) )
			success = TRUE;
		break;
	}

	return success;
}

ClientResponse ServerInterface::PingClient(long cuid) {
	if ( !ClientIsInClientList(cuid) )
		return {};

	ClientData clientInfo = GetClientData(cuid);
	Client     client     = clientInfo.first;
	if ( !client.SocketReady(TCP) ) // socket isnt ready so cant ping.
		return {};

	// send the ping to the client over tcp
	ServerCommand pingCommand = { true, {}, "", client.RSAPublicKey, PING_CLIENT};
	BOOL sent = TCPSendMessageToClient(cuid, pingCommand);
	if ( !sent )
		return {};

	return WaitForClientResponse(cuid);
}

BOOL ServerInterface::ClientIsInClientList(long cuid) {
	try {
		ClientListMutex.lock();
		ClientData cd = GetClientList().at(cuid);
		ClientListMutex.unlock();
	}
	catch ( const std::out_of_range& ) {
		ClientListMutex.unlock();
		return FALSE;
	}
	return TRUE;
}

BOOL ServerInterface::AddToClientList(Client client) {
	long cuid = client.ClientUID;
	
	// generate a cuid that isnt in use
	while ( cuid != -1 && ClientIsInClientList(cuid) ) // keep generating if cuid is in use
		cuid = client.GenerateCUID();

	client.ClientUID = cuid;
	
	ClientListMutex.lock();
	this->ClientList[cuid] = std::make_pair(client, std::make_pair(client.RSAPublicKey, client.RSAPrivateKey) );
	ClientListMutex.unlock();
	
	// client has been correctly inserted as a tuple into clientlist
	return this->ClientList.at(cuid).first.TCPSocket == client.TCPSocket;
}

BOOL ServerInterface::IsClientAlive(long cuid) {
	// Check if client is in ClientList and exists
	if ( !ClientIsInClientList(cuid) )
		return FALSE; // Client doesn't exist. Nothing returned from GetClientData

	ClientData clientInfo = GetClientData(cuid);
	Client client = clientInfo.first;

	if ( client.SocketReady(TCP) == FALSE )
		return FALSE; // socket not setup 

	// Check if client sends and receives ping
	if ( PingClient(cuid).responseCode != C_OK )
		return FALSE; // Client is dead

	return TRUE; // Client is alive
}

template <typename Data>
Data ServerInterface::DecryptClientData(BYTESTRING cipher, long cuid) {
	if ( !ClientIsInClientList(cuid) )
		return {};

	ClientData  clientInfo    = GetClientData(cuid);
	std::string decryptionKey = clientInfo.second.first;
	Data        decrypted     = NetCommon::DecryptInternetData<Data>(cipher, decryptionKey);
	
	return decrypted;
}

ClientRequest ServerInterface::DecryptClientRequest(long cuid, BYTESTRING req) {
	return DecryptClientData<ClientRequest>(req, cuid); // return decrypted clientRequest struct
}

ClientResponse ServerInterface::DecryptClientResponse(long cuid, BYTESTRING resp) {
	return DecryptClientData<ClientResponse>(resp, cuid);
}

BYTESTRING ServerInterface::EncryptServerRequest(ServerRequest& req) {
	NET_BLOB blob = NetCommon::RequestToBlob(req, req.publicEncryptionKey);
	BYTESTRING cipher = NetCommon::AESEncryptBlob(blob);

	return cipher;
}

#endif // SERVER_RELEASE