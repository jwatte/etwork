
//  chathost.h
//  This file defined a simple interface for a service that 
//  hosts text chat over a specific port.
//  Intended as a sample for how to use the message-layer 
//  functionality of the Etwork networking library.
//  http://www.mindcontrol.org/etwork/

#if !defined( chathost_h )
#define chathost_h

//  Information kept about each client.
struct ChatClient {
  char name[32];      //  maximum name size (capped)
  char address[32];   //  xxx.yyy.zzz.www:port
  double lastReceive; //  time of last receive, using host clock
  size_t numMessages; //  number of messages sent on behalf of user
};

//  IChatHost is the abstract interface for the chat host.
//  You create one using NewChatHost(), and close it down 
//  by calling dispose().
class IChatHost {
  public:
    //  kick (remove) a user, by name
    virtual bool kickUser( char const * user ) = 0;
    //  service the chat users (and underlying network) every so often
    virtual void poll( double time ) = 0;
    //  what is the current generation of the client list? (used to detect changes)
    virtual size_t clientGeneration() = 0;
    //  how many clients are there?
    virtual size_t countClients() = 0;
    //  get information about client N in the list
    virtual bool getClient( size_t index, ChatClient * outInfo ) = 0;
    //  what's the current host clock? (in seconds since start)
    virtual double time() = 0;
    //  close down all the networking/server functionality
    virtual void dispose() = 0;
};

//  Create an instance of the chat host, running on the specified 
//  port, using TCP (if reliable) or UDP (if not).
IChatHost * NewChatHost( short port, bool reliable );

//  The protocol used between chat clients and the server 
//  representation consists of a command byte followed by 
//  data, in each message. The command byte is defined here 
//  (although a user of IChatHost doesn't need to know 
//  about it at all).
enum {
  //  client->server
  MsgLogin = 128,
  MsgTextToServer = 129,
  MsgLogout = 130,

  //  server->client
  MsgTextFromServer = 192,
  MsgLoginAccepted = 193,
  MsgNoLogin = 194,
};

#endif  //  chathost_h
