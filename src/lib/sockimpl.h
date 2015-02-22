#if !defined( etwork_sockimpl_h )
#define etwork_sockimpl_h

#include "etwork/etwork.h"
#include "etwork/locker.h"
#include "etwork/buffer.h"
#include "etwork/timer.h"
#include "etwork/errors.h"
#include "etwork/notify.h"

#if defined( WIN32 )
#include <windows.h>
#endif

#include <stdio.h>  //  for _snprintf
#include <math.h>

#include <string>
#include <map>
#include <set>
#include <deque>
#include <algorithm>
#include <new>

#include "eimpl.h"


namespace etwork {
  class Socket;
  class SocketManager : public ISocketManager {
    public:
      SocketManager();
      ~SocketManager();
      bool open( EtworkSettings * settings );

      //  ISocketManager
      virtual int poll( double seconds, ISocket ** outActive, int maxActive );
      virtual int accept( ISocket ** outAccepted, int maxAccepted );
      virtual int connect( char const * address, unsigned short port, ISocket ** outConnected );
      virtual void dispose();

      void debug_sock_error( ISocket * sock, int err, ErrorArea area, char const * func );
      void remove_socket( Socket * s );
      void regenerate_sets();
      SOCKET socket_id();
      bool handle_listening_read( size_t maxActive );
      bool handle_listening_write( size_t maxActive );
      bool handle_listening_except( size_t maxActive );
      void change_queuing_space();
      void timeout_sockets();

      EtworkSettings settings_;
      SOCKET listening_;
      typedef std::map< SOCKET, Socket * > SocketMap;
      SocketMap sockets_;
      typedef std::map< sockaddr_in, Socket * > AddressMap;
      AddressMap socketAddrs_;
      std::deque< Socket * > accepted_;
      Timer time_;
      std::set< ISocket * > active_;
      std::set< ISocket * > notify_;

      size_t maxNumSocks_;
      size_t numSocks_;
      size_t maxSock_;
      fd_set * allSet_;
      fd_set * readSet_;
      fd_set * writeSet_;
      fd_set * writeTempSet_;
      fd_set * exceptSet_;
      char * tmpBuffer_;

      int nextSocket_;
      int curQueueSpace_;
      double curTime_;
  };

  class Socket : public ISocket {
    public:
      Socket( SocketManager * mgr, SOCKET s, sockaddr_in const & addr ) :
        mgr_( mgr ), s_( s ), closed_( false ), accepted_( false ), addr_( addr ),
        bufIn_( mgr->settings_.maxMessageSize, mgr->settings_.queueSize, mgr->settings_.maxMessageCount ),
        bufOut_( mgr->settings_.maxMessageSize, mgr->settings_.queueSize, mgr->settings_.maxMessageCount )
      {
        if( !mgr->settings_.reliable ) {
          s_ = mgr->socket_id();
        }
        writebuf_ = new char[ mgr->settings_.queueSize ];
        writebufData_ = 0;
        data_ = 0;    //  this is the only time I touch the "data" member
        notify_ = 0;
        lastActive_ = mgr_->curTime_;
        lastKeepalive_ = 0;
      }
      ~Socket() {
        close_socket();
        delete[] writebuf_;
      }

      //  ISocket
      virtual sockaddr_in address();
      virtual int read( void * buffer, size_t maxSize );
      virtual int write( void const * buffer, size_t size );
      virtual bool closed();
      virtual void dispose();

      bool wants_to_write()
      {
        return writebufData_ > 0 || bufOut_.message_count() > 0;
      }
      bool wants_to_read()
      {
        return bufOut_.space_used() < mgr_->settings_.queueSize-mgr_->settings_.maxMessageSize
            && bufOut_.message_count() < mgr_->settings_.maxMessageCount;
      }
      bool do_read();
      bool do_write();
      bool do_except();
      void close_socket()
      {
        if( !closed_ ) {
          closed_ = true;
          mgr_->remove_socket( this );
          if( !IS_SOCKET_ERROR( s_ ) && mgr_->settings_.reliable ) {
            ::closesocket( s_ );
          }
          s_ = INVALID_SOCKET;
        }
      }

      SocketManager * mgr_;
      INotify * notify_;
      SOCKET s_;
      bool closed_;
      bool accepted_;
      sockaddr_in addr_;
      Buffer bufIn_;
      Buffer bufOut_;
      double lastActive_; //  for timeouts
      double lastKeepalive_;

      //  because of async I/O, I actually need a third buffer for writing
      char * writebuf_;
      size_t writebufData_;
  };
}

#endif  //  etwork_sockimpl_h
