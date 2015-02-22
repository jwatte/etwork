
#include "sockimpl.h"

using namespace etwork;
using namespace etwork::impl;


SocketManager::SocketManager()
{
  listening_ = INVALID_SOCKET;
  maxNumSocks_ = FD_SETSIZE;
  numSocks_ = 0;
  maxSock_ = 0;
  allSet_ = (fd_set *)::operator new( sizeof(fd_set) );
  FD_ZERO( allSet_ );
  readSet_ = (fd_set *)::operator new( sizeof(fd_set) );
  FD_ZERO( readSet_ );
  writeSet_ = (fd_set *)::operator new( sizeof(fd_set) );
  FD_ZERO( writeSet_ );
  writeTempSet_ = (fd_set *)::operator new( sizeof(fd_set) );
  FD_ZERO( writeTempSet_ );
  exceptSet_ = (fd_set *)::operator new( sizeof(fd_set) );
  FD_ZERO( exceptSet_ );
  nextSocket_ = 1;
  tmpBuffer_ = 0;
  curQueueSpace_ = 0;
  curTime_ = time_.seconds();
}

SocketManager::~SocketManager()
{
  if( listening_ != INVALID_SOCKET ) {
    ::closesocket( listening_ );
    listening_ = INVALID_SOCKET;
  }
  ::operator delete( allSet_ );
  ::operator delete( readSet_ );
  ::operator delete( writeSet_ );
  ::operator delete( writeTempSet_ );
  ::operator delete( exceptSet_ );
  delete[] tmpBuffer_;
}

bool SocketManager::open( EtworkSettings * settings )
{
  settings_ = *settings;

  if( settings_.accepting && !settings_.port ) {
    ErrorInfo ei;
    ei.error = EtworkError( ES_error, EA_init, EO_invalid_parameters );
    ei.error.setText( "Port may not be 0 when accepting in EtworkSettings." );
    ei.osError = 0;
    ei.socket = 0;
    etwork_info_from( 0, ei );
    return false;
  }
  //  Unreliable socket managers always get one socket,
  //  even if not "accepting" connections.
  if( settings->accepting || !settings->reliable ) {
    int type = SOCK_STREAM;
    int proto = IPPROTO_TCP;
    if( !settings->reliable ) {
      type = SOCK_DGRAM;
      proto = IPPROTO_UDP;
    }
    listening_ = ::socket( AF_INET, type, proto );
    if( IS_SOCKET_ERROR( listening_ ) ) {
      debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::socket(AF_INET)" );
      return false;
    }
    int one = 1;
    int r = ::setsockopt( listening_, SOL_SOCKET, SO_REUSEADDR, (char const *)&one, sizeof(one) );
    if( r < 0 ) {
      debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::setsockopt(SO_REUSEADDR)" );
      goto failure;
    }
    sockaddr_in addr;
    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_port = htons( settings->port );
    r = ::bind( listening_, (sockaddr const *)&addr, (int)sizeof(addr) );
    if( r < 0 ) {
      debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::bind()" );
      goto failure;
    }
    if( settings->reliable ) {
      r = ::listen( listening_, 100 );
      if( r < 0 ) {
        debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::listen(100)" );
        goto failure;
      }
    }
    else {
      //  make the socket non-blocking
      u_long nonblock = 1;
      r = ::ioctlsocket( listening_, FIONBIO, &nonblock );
      if( r < 0 ) {
        debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::ioctlsocket(FIONBIO)" );
        goto failure;
      }
      //  make sure there's enough queuing space
      change_queuing_space();
    }
  }
  tmpBuffer_ = new char[ settings_.maxMessageSize ];
  if( settings->debug ) {
    OutputDebugString( "Etwork: SocketManager::open() was succesful.\n" );
  }
  regenerate_sets();
  return true;

failure:
  ::closesocket( listening_ );
  listening_ = INVALID_SOCKET;
  return false;
}

namespace {
  struct NotifyActive {
    std::set< ISocket * > & s_;
    NotifyActive( std::set< ISocket * > & s ) : s_( s ) {
    }
    ~NotifyActive() {
      std::set< ISocket * > tmp;
      tmp.swap( s_ );
      std::for_each( tmp.begin(), tmp.end(), notify );
    }
    static void notify( ISocket * s ) {
      Socket * ss = static_cast< Socket * >( s );
      if( !ss->notify_ ) {
        //  The socket notify was removed from this socket while in-flight!
        //  User won't really hear about this through the regular channel.
        etwork_error_from( ss, ss->mgr_, EtworkError( ES_warning, EA_session, EO_internal_error ) );
      }
      else {
        ss->notify_->onNotify();
      }
    }
  };
}

void SocketManager::timeout_sockets()
{
  for( SocketMap::iterator ptr = sockets_.begin(), end = sockets_.end(); ptr != end; ) {
    Socket *cl = (*ptr).second;
    bool remove = false;
    if( settings_.timeout > 0 && cl->lastActive_ + settings_.timeout < curTime_ ) {
      etwork_error_from( cl, this, EtworkError( ES_note, EA_session, EO_peer_timeout ) );
      remove = true;
    }
    else if( settings_.keepalive > 0 && cl->lastKeepalive_ + settings_.keepalive < curTime_ ) {
      //  send keepalive message
      cl->write( "", 0 );
    }
    ++ptr;
    if( remove ) {
      cl->close_socket();
      if( cl->accepted_ ) {
        if( cl->notify_ ) {
          notify_.insert( cl );
        }
        else {
          active_.insert( cl );
        }
      }
    }
  }
}

int SocketManager::poll( double seconds, ISocket ** outActive, int maxActive )
{
  if( maxActive < 1 || !outActive ) {
    debug_sock_error( 0, WSAEINVAL, EA_session, "SocketManager::poll() maxActive" );
    return -1;
  }

  memset( outActive, 0, sizeof( *outActive )*maxActive );
  active_.clear();
  NotifyActive na( notify_ );   //  Make sure they get notified on all exit paths.
                                //  Also, NotifyActive clears the set after notification.

  //  handle timeouts
  double now = time_.seconds();
  curTime_ = now;
  timeout_sockets();

  if( seconds < 0 ) {
    seconds = 0;
  }

  //  Only write to sockets the first time I poll in a given timeout period,
  //  unless they show to have activity (making progress).
  memcpy( writeSet_, allSet_, sizeof(fd_set)+sizeof(SOCKET)*(numSocks_-FD_SETSIZE) );

again:
#if defined( WIN32 )
  if( numSocks_ == 0 ) {
    //  no sockets to poll anymore -- return what we have
    std::copy( active_.begin(), active_.end(), outActive );
    return (int)active_.size();
  }
  //  I do this copying around of socket sets to avoid having to
  //  walk each socket struct for each select() -- doing that
  //  would take a lot of cahce misses, and thus be slow.
  //  (I'm kidding myself -- I end up walking a lot of them later
  //  in this function anyway...)
  memcpy( readSet_, allSet_, sizeof(fd_set)+sizeof(SOCKET)*(numSocks_-FD_SETSIZE) );
  memcpy( exceptSet_, allSet_, sizeof(fd_set)+sizeof(SOCKET)*(numSocks_-FD_SETSIZE) );
#else
#error "implement me!"
#endif

  //  Calculate timeout
  curTime_ = time_.seconds();
  double then = seconds - curTime_ + now;
  if( then < 0 ) {
    then = 0;
  }
  timeval timo;
  timo.tv_sec = (int)floor( then );
  timo.tv_usec = (int)((seconds-timo.tv_sec)*1000000);
  int r = ::select( (int)maxSock_, readSet_, writeSet_, exceptSet_, &timo );
  // Handle error case
  if( r < 0 ) {
    debug_sock_error( 0, WSAGetLastError(), EA_session, "::select()" );
    if( active_.size() ) {
      std::copy( active_.begin(), active_.end(), outActive );
      if( settings_.debug ) {
        OutputDebugString( "select() failed but returning active sockets\n" );
      }
      return (int)active_.size();
    }
    return -1;
  }

  //  Start out assuming no sockets will be writing the next time around.
  FD_ZERO( writeTempSet_ );
  bool progress = true;
#if defined( WIN32 )
  for( size_t i = 0; i < readSet_->fd_count; ++i ) {
    //  service read
    SOCKET s = readSet_->fd_array[i];
#else
#error "implement me"
#endif
    if( s == listening_ ) {
      //  Listening read will also do unreliable sockets.
      //  Return false if it's time to exit out of the read loop.
      if( !handle_listening_read( maxActive ) ) {
        if( settings_.debug ) {
          OutputDebugString( "handle_listening_read() failed.\n" );
        }
        progress = false;
      }
    }
    else {
      //  look up the socket handler for this fd
      SocketMap::iterator ptr = sockets_.find( s );
      ASSERT( ptr != sockets_.end() );
      if( ptr != sockets_.end() ) {
        Socket * s = (*ptr).second;
        //  Tell the socket to put data into its message queue.
        //  Return false if it's time to exit out of the read loop.
        if( s->wants_to_read() ) {
          if( !s->do_read() ) {
            if( settings_.debug ) {
              OutputDebugString( "Socket->do_read() failed.\n" );
            }
            progress = false;
          }
          if( s->notify_ ) {
            notify_.insert( s );
          }
          else {
            active_.insert( s );
          }
        }
        //  this socket has activity -- give it another chance at writing
        if( progress && s->bufOut_.space_used() > 0 && !s->closed() ) {
          FD_SET( (*ptr).first, writeTempSet_ );
        }
      }
    }
    if( active_.size() == maxActive ) {
      if( settings_.debug ) {
        OutputDebugString( "Etwork: SocketManager::poll() filled up the active socket array in read.\n" );
      }
      goto no_more_actives;
    }
#if defined( WIN32 )
  }
  for( size_t i = 0; i < writeSet_->fd_count; ++i ) {
    //  service write
    SOCKET s = writeSet_->fd_array[i];
#else
#error "implement me"
#endif
    if( s == listening_ ) {
      if( !handle_listening_write( maxActive ) ) {
        if( settings_.debug ) {
          OutputDebugString( "handle_listening_write() failed.\n" );
        }
        progress = false;
      }
    }
    else {
      SocketMap::iterator ptr = sockets_.find( s );
      //  socket may close inside read()
      if( ptr != sockets_.end() ) {
        if( (*ptr).second->wants_to_write() ) {
          if( !(*ptr).second->do_write() ) {
            if( settings_.debug ) {
              OutputDebugString( "Socket->do_write() failed.\n" );
            }
            progress = false;
          }
          else {
            if( (*ptr).second->notify_ ) {
              notify_.insert( (*ptr).second );
            }
            else {
              active_.insert( (*ptr).second );
            }
          }
          //  this socket has activity -- give it another chance at writing
          if( progress && (*ptr).second->wants_to_write() ) {
            FD_SET( (*ptr).first, writeTempSet_ );
          }
        }
        //  I don't add back sockets that have writebufData_, because those
        //  mean that they are behind on their window size, so I should
        //  wait trying to ram more data down their throat anyway.
      }
    }
    if( active_.size() == maxActive ) {
      if( settings_.debug ) {
        OutputDebugString( "Etwork: SocketManager::poll() filled up the active socket array in write.\n" );
      }
      goto no_more_actives;
    }
#if defined( WIN32 )
  }
  for( size_t i = 0; i < exceptSet_->fd_count; ++i ) {
    //  service except
    SOCKET s = exceptSet_->fd_array[i];
#else
#error "implement me"
#endif
    if( s == listening_ ) {
      if( !handle_listening_except( maxActive ) ) {
        if( settings_.debug ) {
          OutputDebugString( "handle_listening_except() failed.\n" );
        }
        progress = false;
      }
    }
    else {
      SocketMap::iterator ptr = sockets_.find( s );
      //  sockets may close within read()
      if( ptr != sockets_.end() ) {
        if( !(*ptr).second->do_except() ) {
          if( settings_.debug ) {
            OutputDebugString( "Socket->do_except() failed.\n" );
          }
          progress = false;
        }
        if( (*ptr).second->notify_ ) {
          notify_.insert( (*ptr).second );
        }
        else {
          active_.insert( (*ptr).second );
        }
      }
    }
    if( active_.size() == maxActive ) {
      if( settings_.debug ) {
        OutputDebugString( "Etwork: SocketManager::poll() filled up the active socket array in except.\n" );
      }
      goto no_more_actives;
    }
#if defined( WIN32 )
  }
#else
#error "implement me"
#endif
  then = time_.seconds();
  //  If I made progress (i e, no-one spin-reading on a full buffer), then
  //  I may consider going back for seconds until the timeout runs out.
  if( then-now < seconds && progress ) {
#if defined( WIN32 )
    memcpy( writeSet_, writeTempSet_, sizeof(((fd_set *)0)->fd_count)+sizeof(SOCKET)*numSocks_ );
#else
#error "implement me"
#endif
    goto again;
  }
no_more_actives:
  std::copy( active_.begin(), active_.end(), outActive );
  return (int)active_.size();
}

int SocketManager::accept( ISocket ** outAccepted, int maxAccepted )
{
  memset( outAccepted, 0, sizeof(*outAccepted)*maxAccepted );
  int i = 0;
  bool changed = false;
  for( ; i < maxAccepted; ++i ) {
    if( !accepted_.size() ) {
      break;
    }
    Socket * s = accepted_.front();
    accepted_.pop_front();
    // note that s_ is a "socket id" for unreliable sockets
    sockets_[s->s_] = s;
    changed = true;
    outAccepted[i] = s;
    s->accepted_ = true;
  }
  if( changed ) {
    change_queuing_space();
    regenerate_sets();
  }
  return i;
}

int SocketManager::connect( char const * address, unsigned short port, ISocket ** outConnected )
{
  *outConnected = 0;
  sockaddr_in addr;
  memset( &addr, 0, sizeof( addr ) );
  addr.sin_family = AF_INET;
  addr.sin_port = htons( port );
  {
    //  gethostbyname() is not thread-safe, even across instances
    Locker ghl( gethostLock );
    hostent * ent = gethostbyname( address );
    if( !ent ) {
      debug_sock_error( 0, WSAGetLastError(), EA_address, "::gethostbyname()" );
      return -1;
    }
    memcpy( &addr.sin_addr, ent->h_addr_list[0], sizeof(addr.sin_addr) );
  }

  SOCKET s;
  if( settings_.reliable ) {
    s = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( IS_SOCKET_ERROR( s ) ) {
      debug_sock_error( 0, WSAGetLastError(), EA_connect, "::socket(AF_INET)" );
      return -1;
    }
    int r = ::connect( s, (sockaddr const *)&addr, (int)sizeof( addr ) );
    if( r < 0 ) {
      debug_sock_error( 0, WSAGetLastError(), EA_connect, "::connect()" );
      ::closesocket( s );
      return -1;
    }
    int one = 1;
    r = ::setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char const *)&one, sizeof( one ) );
    if( r < 0 ) {
      debug_sock_error( 0, WSAGetLastError(), EA_connect, "::setsockopt(TCP_NODELAY)" );
    }
  }
  else {
    s = listening_;
  }
  //  @TODO: There is a potential race here, where we may have a socket 
  //  waiting inside accepting_ but not yet accepted, yet the client 
  //  calls connect() on that address. Figure out what to do: return the 
  //  accepted socket? Delete the accepted socket? (that might lead to 
  //  live-lock if we're unlucky)
  Socket * so = new Socket( this, s, addr );
  //  note that s_ is a "socket id" for unreliable sockets
  sockets_[so->s_] = so;
  *outConnected = so;
  so->accepted_ = true;
  regenerate_sets();
  if( !settings_.reliable ) {
    //  gotta make sure that we can find this socket again
    socketAddrs_[addr] = so;
    so->write( "", 0 ); //  send an empty packet to establish a connection
  }
  return 1;
}

void SocketManager::dispose()
{
  if( sockets_.size() ) {
    char buf[2048];
    _snprintf( buf, 2048, "Etwork: SocketManager::dispose() sees %d active sockets.\n", sockets_.size() );
    buf[2047] = 0;
    OutputDebugString( buf );
    if( settings_.debug ) {
      __asm { int 3 }
    }
  }
  if( accepted_.size() ) {
    char buf[2048];
    _snprintf( buf, 2048, "Etwork: SocketManager::dispose() sees %d sockets pending accept.\n", accepted_.size() );
    buf[2047] = 0;
    OutputDebugString( buf );
    if( settings_.debug ) {
      __asm { int 3 }
    }
  }
  delete this;
}

void SocketManager::debug_sock_error( ISocket * sock, int err, ErrorArea area, char const * func )
{
  wsa_error_from( sock, this, err, area );
}

void SocketManager::remove_socket( Socket * s )
{
  socketAddrs_.erase( s->addr_ );
  SocketMap::iterator ptr = sockets_.find( s->s_ );
  if( ptr != sockets_.end() ) {
    sockets_.erase( ptr );
    if( settings_.reliable ) {
      regenerate_sets();
    }
    return;
  }
  std::deque< Socket * >::iterator q = std::find( accepted_.begin(), accepted_.end(), s );
  if( q != accepted_.end() ) {
    accepted_.erase( q );
    return;
  }
  ASSERT( !"Socket not found in SocketManager::remove_socket()" );
}

//  The theory behind regenerate_sets() is that it's more efficient 
//  to create the socket read/write/exception fd_set once, and keep 
//  it around, than having to scrape through all the individual 
//  socket descriptors to generate the set. However, it turns out 
//  that, during typical usage, you'll scrape through most of them 
//  anyway (post-receive), so it's not exactly a big win.
//  However: it's tested, it works, it stays until it becomes a 
//  problem.
void SocketManager::regenerate_sets()
{
  size_t needed = sockets_.size() + 1; // for listening
  if( !settings_.reliable ) {
    needed = 1;
  }
  maxSock_ = 0;
  if( needed > maxNumSocks_ ) {
    maxNumSocks_ = (size_t)(needed * 1.5 + 10);
    ::operator delete( allSet_ );
    ::operator delete( readSet_ );
    ::operator delete( writeSet_ );
    ::operator delete( writeTempSet_ );
    ::operator delete( exceptSet_ );
#if defined( WIN32 )
    fd_set * nu = (fd_set *)::operator new( sizeof( fd_set ) + sizeof(SOCKET)*(maxNumSocks_-FD_SETSIZE) );
    FD_ZERO( nu );
    allSet_ = nu;
    nu = (fd_set *)::operator new( sizeof( fd_set ) + sizeof(SOCKET)*(maxNumSocks_-FD_SETSIZE) );
    FD_ZERO( nu );
    readSet_ = nu;
    nu = (fd_set *)::operator new( sizeof( fd_set ) + sizeof(SOCKET)*(maxNumSocks_-FD_SETSIZE) );
    FD_ZERO( nu );
    writeSet_ = nu;
    nu = (fd_set *)::operator new( sizeof( fd_set ) + sizeof(SOCKET)*(maxNumSocks_-FD_SETSIZE) );
    FD_ZERO( nu );
    writeTempSet_ = nu;
    nu = (fd_set *)::operator new( sizeof( fd_set ) + sizeof(SOCKET)*(maxNumSocks_-FD_SETSIZE) );
    FD_ZERO( nu );
    exceptSet_ = nu;
#else
#error "implement me!"
#endif
  }
  numSocks_ = needed-1;
  FD_ZERO( allSet_ );
  if( !IS_SOCKET_ERROR( listening_ ) ) {
    FD_SET( listening_, allSet_ );
    numSocks_ = needed;
    if( listening_ > maxSock_ ) {
      maxSock_ = listening_;
    }
  }
  if( settings_.reliable ) {
    SocketMap::iterator ptr = sockets_.begin(), end = sockets_.end();
    while( ptr != end ) {
      FD_SET( (*ptr).first, allSet_ );
      if( (*ptr).first > maxSock_ ) {
        maxSock_ = (*ptr).first;
      }
      ++ptr;
    }
  }
}

//  mint a "socket id" which can be used to identify an
//  unreliable socket.
SOCKET SocketManager::socket_id()
{
  SOCKET ret;
again:
  ret = (SOCKET)nextSocket_;
  ++nextSocket_;
  if( !nextSocket_ ) {
    ++nextSocket_;
  }
  if( ret == listening_ || sockets_.find( ret ) != sockets_.end() ) {
    goto again;
  }
  return ret;
}


bool SocketManager::handle_listening_except( size_t maxActive )
{
  int error = 0;
  int errorsize = sizeof( error );
  int r = ::getsockopt( listening_, SOL_SOCKET, SO_ERROR, (char *)&error, &errorsize );
  ASSERT( r == 4 || !"getsockopt() on listening_ SO_ERROR failed" );
  debug_sock_error( 0, error, EA_session, "SocketManager::handle_listening_except() getsockopt() SO_ERROR" );
  //  This means either that the listening socket has an issue,
  //  or that I'm unreliable and some send failed.
  if( settings_.reliable ) {
    //  what to do?
  }
  else {
    //  check the error; if it's could-not-send, then see what sockets
    //  have sent in the meanwhile
  }
  return false;
}

bool SocketManager::handle_listening_read( size_t maxActive )
{
  ASSERT( maxActive > active_.size() );
  if( settings_.reliable ) {
    //  If I'm reliable, this means I should accept().
    sockaddr_in addr;
    int alen = sizeof( addr );
    SOCKET so = ::accept( listening_, (sockaddr *)&addr, &alen );
    if( IS_SOCKET_ERROR( so ) ) {
      debug_sock_error( 0, ::WSAGetLastError(), EA_session, "::accept()" );
      return false;
    }
    int one = 1;
    int r = ::setsockopt( so, IPPROTO_TCP, TCP_NODELAY, (char const *)&one, sizeof(one) );
    Socket * s = new Socket( this, so, addr );
    accepted_.push_back( s );
  }
  else {
    //  If I'm unreliable, it means that I should recvfrom().
    while( true ) {
      sockaddr_in addr;
      int alen = sizeof( addr );
      int r = ::recvfrom( listening_, tmpBuffer_, (int)settings_.maxMessageSize, 0, (sockaddr *)&addr, &alen );
      if( r < 0 ) {
        break;
      }
      AddressMap::iterator ptr = socketAddrs_.find( addr );
      Socket * s = 0;
      if( ptr == socketAddrs_.end() ) {
        if( settings_.accepting ) { //  only accept new clients if "accepting" is true
          //  a new unreliable socket
          s = new Socket( this, 0, addr );
          accepted_.push_back( s );
          socketAddrs_[addr] = s;
          //   Special case: write an empty packet to acknowledge connection.
          //   This acknowledgement may not actually get there, of course.
          int w = ::sendto( listening_, "", 0, 0, (sockaddr const *)&addr, sizeof(addr) );
          if( w < 0 ) {
            debug_sock_error( 0, ::WSAGetLastError(), EA_session, "::sendto() during accept()" );
          }
        }
        //  else just drop it on the floor -- we didn't ask for it!
        continue;
      }
      else {
        s = (*ptr).second;
        if( s->accepted_ ) {
          if( s->notify_ ) {
            notify_.insert( s );
          }
          else {
            active_.insert( s );
          }
          s->lastActive_ = curTime_;
        }
      }
      int p = s->bufIn_.put_message( tmpBuffer_, r );
      if( p < 0 ) {
        etwork_error_from( s->accepted_ ? s : 0, this, EtworkError( ES_warning, EA_session, EO_buffer_full ) );
      }
    }
    //  Unreliable receive always ends in an error (although it may just be wouldblock).
    int error = ::WSAGetLastError();
    switch( error ) {
      case WSAEWOULDBLOCK:
        //  OK, we drained the input!
        break;
      default:
        //  I'm wondering about WSAECONNREFUSED here
        debug_sock_error( 0, error, EA_session, "Etwork: ::recv() on UDP socket" );
        break;
    }
  }
  return true;
}

bool SocketManager::handle_listening_write( size_t maxActive )
{
  //  If I'm reliable, what does this mean?
  if( settings_.reliable ) {
    ASSERT( !"what does it mean to get a write ability on a listening socket?" );
    return false;
  }
  //  If I'm unreliable, it means that I should sendto().
  //  But from whom?
  for( SocketMap::iterator ptr = sockets_.begin(), end = sockets_.end();
      ptr != end; ++ptr ) {
    Socket * s = (*ptr).second;
    while( s->wants_to_write() ) {
      int r = s->bufOut_.get_message( tmpBuffer_, settings_.maxMessageSize );
      ASSERT( r >= 0 || !"impossible message accepted in s->bufOut_" );
      int w = ::sendto( listening_, tmpBuffer_, r, 0, (sockaddr const *)&s->addr_, sizeof( s->addr_ ) );
      if( w < 0 ) {
        //  probably filled the sending buffer
        goto error_writing;
      }
      if( s->notify_ ) {
        notify_.insert( s );
      }
      else {
        active_.insert( s );
      }
      s->lastKeepalive_ = curTime_;
      if( active_.size() == maxActive ) {
        //  can't send more, because that might cause overflow in active buffer
        break;
      }
    }
  }
  return true;  //  I've done all the writing I can think of doing!

error_writing:
  int error = ::WSAGetLastError();
  debug_sock_error( 0, error, EA_session, "::sendto()" );
  switch( error ) {
    case WSAEWOULDBLOCK:
      //  this is OK, just means I filled up the queue
      return true;
      break;
    default:
      //  this is more serious!
      break;
  }
  return false;
}

void SocketManager::change_queuing_space()
{
  int perSocket = (int)settings_.queueSize;
  if( perSocket < 1024 ) {    //  I have certain minimum standards.
    perSocket = 1024;
  }
  int needed = (1+(int)sockets_.size()) * (int)perSocket;
  if( needed < 4096 ) {       //  Don't make it ridiculously small.
    needed = 4096;
  }
  if( needed > curQueueSpace_ ) {
    //  When accepting, make sure that there's space for some more 
    //  clients that might connect.
    if( settings_.accepting ) {
      needed += 5 * (int)settings_.queueSize;
    }
    int r = setsockopt( listening_, SOL_SOCKET, SO_SNDBUF, (char const*)&needed, sizeof( needed ) );
    if( r < 0 ) {
      debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::change_queuing_space(SO_SNDBUF)" );
    }
    else {
      r = setsockopt( listening_, SOL_SOCKET, SO_RCVBUF, (char const*)&needed, sizeof( needed ) );
      if( r < 0 ) {
        debug_sock_error( 0, ::WSAGetLastError(), EA_init, "::change_queuing_space(SO_RCVBUF)" );
      }
      else {
        curQueueSpace_ = needed;
      }
    }
  }
}





sockaddr_in Socket::address()
{
  return addr_;
}

int Socket::read( void * buffer, size_t maxSize )
{
  return bufIn_.get_message( buffer, maxSize );
}

int Socket::write( void const * buffer, size_t size )
{
  return bufOut_.put_message( buffer, size );
}

bool Socket::closed()
{
  return closed_;
}

void Socket::dispose()
{
  delete this;
}

bool Socket::do_read()
{
  ASSERT( mgr_->settings_.reliable );
  int r = ::recv( s_, mgr_->tmpBuffer_, (int)mgr_->settings_.maxMessageSize, 0 );
  if( r < 0 ) {
    int err = WSAGetLastError();
    mgr_->debug_sock_error( this, err, EA_session, "::recv() in Socket::do_read()" );
    switch( err ) {

      case WSAEWOULDBLOCK: {
        //  this is OK
      }
      return false;

      default: {
        //  socket is closed because of error!
        close_socket();
      }
      return false;
    }
  }
  if( r == 0 ) {
    //  this means the socket closed!
    close_socket();
    return true;
  }
  lastActive_ = mgr_->curTime_;
  int w = bufIn_.put_data( mgr_->tmpBuffer_, r );
  if( w < 0 ) {
    etwork_error_from( this, mgr_, EtworkError( ES_warning, EA_session, EO_buffer_full ) );
    return false;
  }
  return true;
}

bool Socket::do_write()
{
  ASSERT( mgr_->settings_.reliable );
  int r = (int)writebufData_;
  if( writebufData_ == 0 ) {
    r = bufOut_.get_data( writebuf_, mgr_->settings_.queueSize );
  }
  ASSERT( r >= 0 || !"bufOut_ error in Socket::do_write()" );
  if( r < 0 ) {
    return false;
  }
  if( r == 0 ) {
    return true;  //  nothing to write
  }
  writebufData_ = r;
  int w = ::send( s_, writebuf_, (int)writebufData_, 0 );
  if( w < 0 ) {
    int err = WSAGetLastError();
    mgr_->debug_sock_error( this, err, EA_session, "::send() in Socket::do_write()" );
    switch( err ) {

      case WSAEWOULDBLOCK: {
        //  this doesn't matter (much)
        w = 0;
        goto send_success;
      }
      break;

      default: {
        //  this error means we need to close
        close_socket();
      }
      return false;
    }
  }
send_success:
  if( w > 0 && w < (int)writebufData_ ) {
    ::memmove( writebuf_, &writebuf_[w], writebufData_-w );
  }
  writebufData_ -= w;
  lastKeepalive_ = mgr_->curTime_;
  return true;
}

bool Socket::do_except()
{
  ASSERT( mgr_->settings_.reliable );
  int error = 0;
  int optlen = sizeof(error);
  int r = ::getsockopt( s_, SOL_SOCKET, SO_ERROR, (char *)&error, &optlen );
  ASSERT( r == 4 );
  switch( error ) {
    case 0:
      mgr_->debug_sock_error( this, 0, EA_session, "Socket::do_except() found no error" );
      break;
    default:
      mgr_->debug_sock_error( this, error, EA_session, "Socket::do_except() socket error" );
      close_socket();
      break;
  }
  return true;
}


void SetEtworkSocketNotify( ISocket * socket, INotify * notify )
{
  Socket * s = static_cast< Socket * >( socket );
  s->notify_ = notify;
}


ISocketManager * CreateEtwork( EtworkSettings * settings )
{
  //  Check that we have settings.
  EtworkSettings defaults;
  if( !settings ) {
    settings = &defaults;
  }

  //  Verify that we can fulfill required version needs.
  if( (settings->etworkVersion & 0xffff) > ((defaults.etworkVersion >> 16)&0xffff) ) {
    OutputDebugString( "CreateEtwork: Requested version is greater than supported version.\n" );
    etwork_error_from( 0, 0, EtworkError( ES_catastrophe, EA_init, EO_unsupported_version ) );
    return 0;
  }

  gDebugging = settings->debug;
  if( settings->maxMessageCount == 0 ) {
    etwork_log( 0, ES_note, "Setting maxMessageCount to 50." );
    settings->maxMessageCount = 50;
  }
  if( settings->maxMessageSize == 0 ) {
    etwork_log( 0, ES_note, "Setting maxMessageSize to 1400." );
    settings->maxMessageSize = 1400;
  }
  if( settings->queueSize == 0 ) {
    etwork_log( 0, ES_note, "Setting maxMessageSize to 4000." );
    settings->queueSize = 4000;
  }
  if( settings->queueSize + settings->maxMessageSize > 65536 ) {
    etwork_log( 0, ES_error, "queueSize + maxMessageSize must be <= 65536." );
    return 0;
  }

  //  Open WinSock if necessary.
  if( !wsOpen ) {
    WSADATA wsaData;
    int i = ::WSAStartup( MAKEWORD(2,2), &wsaData );
    if( i != 0 ) {
      OutputDebugString( "CreateEtwork: WSAStartup() returned error.\n" );
      etwork_error_from( 0, 0, EtworkError( ES_catastrophe, EA_init, EO_unsupported_version ) );
      return 0;
    }
    wsOpen = true;
  }

  //  Create the actual socket manager
  SocketManager * sm = new SocketManager();
  if( !sm->open( settings ) ) {
    sm->dispose();
    return 0;
  }
  return sm;
}

