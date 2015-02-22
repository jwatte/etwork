
//  chathost.cpp
//  Illustrates how you can wrap Etwork into a layer 
//  that understands commands, such as "log in," "log out" 
//  and "pass on this text".

#include "etwork/etwork.h"
#include "etwork/timer.h"
#include "chathost.h"

#include <assert.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <algorithm>


namespace impl {
  class ChatHost;

  //  CChatClient keeps the per-client state.
  class CChatClient : public ChatClient {
    public:
      CChatClient( ISocket * socket, ChatHost * host );
      ~CChatClient();
      void dispose();

      //  The service_ member points at one of these functions, 
      //  depending on what state the client object is in.
      bool serviceDiscovered( double now );
      bool serviceKnown( double now );
      bool serviceKeepalive( double now );

      //  Call this function now and then to make sure that 
      //  the client is properly serviced (timed out, etc).
      bool (CChatClient::*service_)( double now );
      //  The socket that this client connected on.
      ISocket * socket_;
      //  The parent host of this client.
      ChatHost * host_;
      //  The last time we had a keepalive for this client 
      //  (by the hosts clock).
      double lastKeepalive_;
  };
  //  ChatHost is the interface to the main program for the chat 
  //  networking and user management functionality.
  class ChatHost : public IChatHost {
    public:
      //  From IChatHost: forcibly disconnect a connected user
      virtual bool kickUser( char const * user );
      //  From IChatHost: time to service networking and user state
      virtual void poll( double time );
      //  From IChatHost: return different numbers as users come and go.
      //  Can't just use countClients(), because one client may leave 
      //  and another one come within a single tick.
      virtual size_t clientGeneration() { return generation_; }
      //  Return number of connected clients.
      virtual size_t countClients() { return clients_.size(); }
      //  Get information about a specific connected client, by index.
      virtual bool getClient( size_t index, ChatClient * outInfo ) {
        if( index >= clients_.size() ) return false;
        *outInfo = *clients_[index];
        return true;
      }
      //  Return the server's sense of time.
      virtual double time() {
        return time_.seconds();
      }
      //  Dispose the server (make sure that the clients are already gone).
      virtual void dispose() {
        delete this;
      }

      //  When a client connects, the connect packet contains a name. 
      //  Make sure it's not a duplicate. If we had passwords, here's 
      //  where those would be checked as well.
      //  This function also allocates the data for the client if the 
      //  name is new and validates.
      bool validateName( CChatClient * cc ) {
        std::map< std::string, CChatClient * >::iterator ptr = namedClients_.find( cc->name );
        if( ptr != namedClients_.end() ) {
          if( (*ptr).second == cc ) {
            //  If client C wants to re-validate its name, that's cool.
            return true;
          }
          //  Some other client already has this name.
          return false;
        }
        //  Remember this client for future use.
        namedClients_[cc->name] = cc;
        //  Let everybody know that this guy joined, including himself.
        ++generation_;
        //  Send a message, letting everybody know what happened.
        unsigned char buf[200];
        int l = _snprintf( (char *)&buf[1], 199, "%s joined.", cc->name );
        buf[0] = MsgTextFromServer;
        broadcastText( 0, (unsigned char const *)buf, l+1 );
        return true;
      }

      //  BroadcastText sends a message to everybody except "client".
      void broadcastText( CChatClient * client, unsigned char const * text, int len );

      ChatHost();
      ~ChatHost();
      //  As part of creation, the host is "opened" to connect to the network.
      bool open( short port, bool reliable );

      //  Need a timer to keep track of time.
      etwork::Timer time_;
      //  Generation count: bump this each time client list changes.
      size_t generation_;
      //  The actual clients.
      std::vector< CChatClient * > clients_;
      //  The Etwork network interface I'm using.
      ISocketManager * mgr_;
      //  Clients by name -- used to accelerate the validateName() 
      //  and kick() functionality.
      std::map< std::string, CChatClient * > namedClients_;
  };
}
using namespace impl;


//  A factory function, returning an abstract interface 
//  for the functionality of hosting a chat server.
//  'port' is the port to serve on
//  'reliable' is true for TCP (else UDP)
IChatHost * NewChatHost( short port, bool reliable )
{
  ChatHost * ret = new ChatHost();
  if( !ret->open( port, reliable ) ) {
    ret->dispose();
    return 0;
  }
  return ret;
}


ChatHost::ChatHost()
{
  generation_ = 0;
  mgr_ = 0;
}

ChatHost::~ChatHost()
{
  //  dispose all the clients
  for( size_t i = 0, e = clients_.size(); i != e; ++i ) {
    clients_[i]->dispose();
  }
  //  dispose the network manager
  mgr_->dispose();
}

//  In the spirit of "constructors don't fail" there's a 
//  separate function to actually open the host after it's 
//  been constructed.
bool ChatHost::open( short port, bool reliable )
{
  assert( !mgr_ );
  //  Set up the various settings we'll need.
  EtworkSettings settings;
  settings.accepting = true;    //  accept clients
  settings.port = port;         //  port to serve on
  settings.reliable = reliable; //  TCP or UDP
  settings.debug = true;        //  want verbose output
  settings.keepalive = 4.5;
  settings.timeout = 20;
  //  Make ourselves a network to talk on.
  mgr_ = CreateEtwork( &settings );
  if( mgr_ == 0 ) {
    return false;
  }
  return true;
}

bool ChatHost::kickUser( char const * user )
{
  //todo: implement me
  assert( 0 );
  return true;
}

void ChatHost::poll( double time )
{
  //  Given some amount of time, make the best of it in an effort 
  //  to service the network, and the connected clients.
  double now = time_.seconds();
  ISocket * active[100];
  int i = mgr_->poll( time, active, 100 );
  //  service old clients
  if( i < 0 ) {
    fprintf( stderr, "Could not poll the SocketManager: exiting\n" );
    exit( 1 );
  }
  //  Remember who we want to kill (if any) so iteration doesn't 
  //  have to be over containers that change.
  std::set< CChatClient * > tokill;
  //  Service the chat client of each socket that was marked as active.
  for( int j = 0; j < i; ++j ) {
    //  We've stashed the "CChatClient" pointer in the "data_" field 
    //  of the ISocket.
    CChatClient * cc = (CChatClient *)active[j]->data_;
    if( !(cc->*(cc->service_))( now ) ) {
      //  returning false from service means it ought to go away
#if !defined( NDEBUG )
      std::string msg = "tokill (poll service): ";
      (((msg += cc->name) += " ") += cc->address) += "\n";
      OutputDebugString( msg.c_str() );
#endif
      tokill.insert( cc );
    }
  }
  //  Notice how much longer the iterator version is to type 
  //  compared to the index version?
  for( std::vector< CChatClient * >::iterator ptr = clients_.begin(), end = clients_.end();
      ptr != end; ++ptr ) {
    if( (*ptr)->lastReceive < now - 60 ) {  //  time out after a minute
      tokill.insert( *ptr );
    }
    else if( (*ptr)->lastKeepalive_ < now - 20 ) {
      //  send a keep-alive 3 times a minute
      if( !(*ptr)->serviceKeepalive( now ) ) {
        tokill.insert( *ptr );
      }
    }
  }
  //  kick those that should not be here anymore
  if( tokill.size() ) {
    ++generation_;    //  update generation count to signal a client list change
  }
  //  Send a message to everyone else for each client that leaves
  std::deque< std::string > toBroadcast;
  for( std::set< CChatClient * >::iterator ptr = tokill.begin(), end = tokill.end();
      ptr != end; ++ptr ) {
    //  Form the message
    std::string str = std::string( (*ptr)->name ) + " left.";
    toBroadcast.push_back( str );
    //  remove from named clients map
    std::map< std::string, CChatClient * >::iterator nc = namedClients_.find( (*ptr)->name );
    namedClients_.erase( nc );
    //  Dispose the actual client.
    (*ptr)->dispose();
    //  And remove the pointer from the list of all clients
    clients_.erase( std::find( clients_.begin(), clients_.end(), *ptr ) );
  }
  //  For each message to broadcast, send it.
  for( std::deque< std::string >::iterator ptr = toBroadcast.begin(), end = toBroadcast.end();
      ptr != end; ++ptr ) {
    //  Enforce a max message size of 200 bytes, eventhough we know it'll be smaller than that.
    unsigned char buf[200];
    assert( (*ptr).length() < 40 ); //  32 bytes for name, plus the text "left."
    buf[0] = MsgTextFromServer;
    memcpy( &buf[1], (*ptr).c_str(), (*ptr).length() );
    broadcastText( 0, buf, (int)(*ptr).length()+1 );
  }
  //  deal with newcomers -- see how many
  i = mgr_->accept( active, 100 );
  if( i > 0 ) {
    ++generation_;    //  signal a client list change
  }
  for( int j = 0; j < i; ++j ) {
    //  welcome new members
    if( clients_.size() >= 99 ) {
      //  don't accept more chatters than an arbitrary max limit of 99
      active[j]->dispose();
    }
    else {
      //  create an instance to deal with state specific to this client
      CChatClient * cc = new CChatClient( active[j], this );
      //  stash our client struct in the data_ field of the socket
      active[j]->data_ = cc;
      //  give the client a chance to catch up
      if( !(cc->*(cc->service_))( now ) ) {
#if !defined( NDEBUG )
        std::string msg = "discard (new client): ";
        (((msg += cc->name) += " ") += cc->address) += "\n";
        OutputDebugString( msg.c_str() );
#endif
        //  oops, it died when starting up!
        cc->dispose();
      }
      else {
        //  This is a client to deal with in the future.
        clients_.push_back( cc );
        cc->lastReceive = now;
      }
    }
  }
}

void ChatHost::broadcastText( CChatClient * client, unsigned char const * text, int len )
{
  for( std::map< std::string, CChatClient * >::iterator ptr = namedClients_.begin(), 
      end = namedClients_.end(); ptr != end; ++ptr ) {
    //  I could filter out the original sender if necessary, 
    //  but that's actually how he sees what he said...
    (*ptr).second->socket_->write( text, len );
  }
}




//  Keeping track of a client, who has a specific socket.
CChatClient::CChatClient( ISocket * socket, ChatHost * host )
{
  name[0] = 0;
  address[0] = 0;
  lastReceive = 0;
  numMessages = 0;
  socket_ = socket;
  host_ = host;
  //  Set the client to the Discovered state.
  service_ = &CChatClient::serviceDiscovered;
  lastKeepalive_ = 0;
}

CChatClient::~CChatClient()
{
  socket_->dispose();
}

void CChatClient::dispose()
{
  delete this;
}

//  Service this client when it is in the Discovered state.
bool CChatClient::serviceDiscovered( double now )
{
  unsigned char buf[2000];
  //  I'm waiting for a valid login message
  int r;
  while( (r = socket_->read( buf, sizeof( buf ) )) >= 0 ) {
    lastReceive = now;
    if( r == 0 ) {  //  a keepalive
      continue;
    }
#if !defined( NDEBUG )
    std::string msg( "serviceDiscovered() received message " );
    char x[20];
    msg += itoa( buf[0], x, 10 );
    msg += " of size ";
    msg += itoa( r, x, 10 );
    msg += ".\n";
    OutputDebugString( msg.c_str() );
#endif
    //  Check each message, looking for messages I recognize.
    switch( buf[0] ) {
      case MsgLogin: {
        //  make sure I don't do any buffer overwrites when extracting the name
        int namelen = r-1;
        if( namelen >= sizeof(name) ) {
          namelen = sizeof(name)-1;
        }
        strncpy( name, (char *)&buf[1], namelen );
        name[namelen] = 0;
        //  see what the host thinks about this particular name
        if( !host_->validateName( this ) ) {
          //  if it didn't work, tell the client the bad news
          name[0] = 0;
          buf[0] = MsgNoLogin;
          socket_->write( buf, 1 );
        }
        else {
          //  acceptancs should be swift!
          buf[0] = MsgLoginAccepted;
          socket_->write( buf, 1 );
          //  set the client to state "Known"
          service_ = &CChatClient::serviceKnown;
          //  generate the "address" field
          strncpy( address, inet_ntoa( socket_->address().sin_addr ), sizeof(address) );
          address[sizeof(address)-1] = 0;
          char * c = &address[strlen(address)];
          //  add the port part of the address only if there is enough space.
          if( c < &address[25] ) {
            _snprintf( c, 8, ":%d", ntohs( socket_->address().sin_port ) );
          }
          ++host_->generation_; //  signal a client list change (somewhat redundantly)
        }
      }
      break;

      case MsgTextToServer: {
        //  If receiving a "text to server" message when in the Discovered 
        //  state (not the Known state), tell the client it's not logged in.
        //  Hopefully, this NAK will kick the client to attempt login again.
        buf[0] = MsgNoLogin;
        socket_->write( buf, 1 );
      }
      break;

      case MsgLogout: {
        //  It is possible to receive a logout, if we receive a 
        //  re-send of a previous message, and allocate a new 
        //  connection for that.
        return false; //  kick the guy!
      }
      break;

      default:
        //  Someone's not following the protocol!
        //  Let's kick them.
        return false;
    }
  }
  //  return success/failure
  return !socket_->closed();
}

//  Service a client that's in the "known" state.
bool CChatClient::serviceKnown( double now )
{
  //  Look for messages containing text or logout.
  //  Also re-affirm logins, if seeing a login request.
  unsigned char buf[2000];
  //  I'm waiting for a valid login message
  int r;
  while( (r = socket_->read( buf, sizeof( buf ) )) >= 0 ) {
    lastReceive = now;
    if( r == 0 ) {  //  a keepalive
      continue;
    }
#if !defined( NDEBUG )
    std::string msg( "serviceKnown() received message " );
    char x[20];
    msg += itoa( buf[0], x, 10 );
    msg += " of size ";
    msg += itoa( r, x, 10 );
    msg += ".\n";
    OutputDebugString( msg.c_str() );
#endif
    //  Check for messages I know how to receive in this state.
    switch( buf[0] ) {

      //  I can get a duplicate login if the login ACK got lost.
      case MsgLogin: {
        int namelen = r-1;
        if( namelen >= sizeof(name) ) {
          namelen = sizeof(name)-1;
        }
        //  make sure I don't do any buffer overwrites
        if( strncmp( name, (char *)&buf[1], namelen ) ) {
          //  someone's trying to switch names -- kick them
          return false;
        }
        strncpy( name, (char *)&buf[1], namelen );
        name[namelen] = 0;
        //  check that the server likes this name
        if( !host_->validateName( this ) ) {
          //  this name is probably taken already
          name[0] = 0;
          //  tell him the bad news
          buf[0] = MsgNoLogin;
          socket_->write( buf, 1 );
          return false;
        }
        else {
          //  OK, you're accepted... again!
          buf[0] = MsgLoginAccepted;
          socket_->write( buf, 1 );
        }
      }
      break;

      case MsgTextToServer: {
        //  Generate a message containing text FROM server, which 
        //  is pre-fixed with the chatter name and a colon.
        //  Enforce a maximum message size.
        //  the '1' is for the colon
        if( r > (int)sizeof(buf)-(int)strlen(name)-1 ) {
          r = sizeof(buf)-(int)strlen(name)-1;
        }
        //  the '1' is for the initial command byte
        //  note that I subtracted 1 from buf size above, and 
        //  subtract one here, so the +2 is safe.
        memmove( &buf[strlen(name)+2], &buf[1], r-1 );
        memmove( &buf[1], name, strlen(name) );
        buf[1+strlen(name)] = ':';
        buf[0] = MsgTextFromServer;
        //  Send this message to everybody.
        host_->broadcastText( this, buf, r+(int)strlen(name)+1 );
        numMessages++;
      }
      break;

      case MsgLogout: {
        return false; //  kick the guy!
      }
      break;
 
      default:
        //  Someone's not following the protocol!
        //  Let's kick them.
        return false;
    }
  }
  return !socket_->closed();
}

//  Service a socket for the Keepalive event (which isn't really a state)
bool CChatClient::serviceKeepalive( double now )
{
  //  If the socket isn't there, make the client go away.
  if( socket_->closed() ) {
    return false;
  }
  //  send an empty message
  socket_->write( "", 0 );
  lastKeepalive_ = now;
  return true;
}
