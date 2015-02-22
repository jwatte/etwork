
//! \page rationale Etwork library rationale
//!
//! Etwork aims to provide a simple, common-sense API to networking. 
//! It allows the user to create a networking server or client with 
//! a minimum of fuss, and send and receive messages without having 
//! to manually do a lot of book-keeping.
//!
//! Etwork is Copyright 2006 Jon Watte, and is released under the 
//! MIT open source license. It is provided with NO WARRANTY and 
//! NO GUARANTEE of merchantability or fitness for a particular 
//! purpose, free of charge.

//! \addtogroup API Core API
//! @{

//! \file etwork.h
//! The Core API contains the interfaces you need to start listening 
//! for clients to connect, and for creating clients and connecting 
//! them to listening servers.
//!
//! Remember that Etwork is a message-based API -- each call to write() 
//! will be matched by a similar call to read() (unless the data is 
//! dropped for some reason, such as the message being bigger than 
//! you allowed when creating the Etwork instance).

#if !defined( etwork_etwork_h )
//! \internal Header guard
#define etwork_etwork_h

#if !defined( ETWORK_API )
//! \internal ETWORK_API is used for exporting symbols from the etwork DLL.
#define ETWORK_API
#endif

#if defined( _WINDOWS_ )
 #if !defined( _WINSOCK2API_ )
  #error "You must include <winsock2.h> before <windows.h>
 #endif
#elif defined( WIN32 )
 #include <winsock2.h>
 #include <windows.h>
#else
 #include <sys/socket.h>
#endif

#include <stdlib.h>
#include <string.h>

class ISocketManager;
class ISocket;
class IErrorNotify;

//! EtworkSettings represents the various global parameter with which 
//! you can configure a specific Etwork networking subsystem instance.
struct EtworkSettings {
  size_t structSize;        //!< structSize is set up by the constructor
  size_t etworkVersion;     //!< etworkVersion is set up by the constructor

  size_t maxMessageCount;   //!< Maximum number of messages allowed within the queue. If 0, defaults to 50.
  size_t maxMessageSize;    //!< Size of the largest message you can send. If 0, defaults to 1400.
  size_t queueSize;         //!< Size of the total queue size used (*2, for input and output). If 0, defaults to 4000.
  unsigned short port;      //!< The port to listen to. If 0, choose any random port.
  bool reliable;            //!< Set to TRUE for TCP; FALSE for UDP.
  bool accepting;           //!< Set to TRUE for servers; FALSE for clients.
  bool debug;               //!< Set to TRUE to enable some debugging output and assertions.

  double keepalive;         //!< Send keepalives this often. If 0, send no keepalibes.
  double timeout;           //!< Time out a connection when it's been idle for this long. If 0, make no timeouts.
  IErrorNotify * notify;    //!< Set to a notifier interface to get notified about errors.

  //! By default, the settings will use game-size buffer and queue sizes, 
  //! with reliable transport, and debugging turned off if you build in 
  //! debug mode.
  EtworkSettings() {
    memset( this, 0, sizeof( *this ) );
    structSize = sizeof( *this );
    reliable = true;
    etworkVersion = 0x13001300;
#if !defined( NDEBUG )
    debug = true;
#endif
  }
};

//! CreateEtwork will open a socket, and bind it to the given port and 
//! listen to it if you set \c accepting to true in the \ref EtworkSettings .
//! If the settings are not \c reliable , a socket will be created even if 
//! you are not \c accepting , although incoming requests from unknown 
//! clients will not be accepted.
//!
//! It will return an \ref ISocketManager 
//! which you can use to service existing sockets, as well as create new 
//! sockets on.
//!
//! @param settings Various options for creating the networking subsystem.
//!
//! @return The created socket manager, representing the networking 
//! subsystem. NULL on failure (typically, the requested port is already 
//! open for servers).
//!
//! @note It is possible to call this function more than once, and thus 
//! have more than one networking subsystem running at the same time. 
//!
//! @note SocketManager is not thread-safe, because it uses no internal 
//! locking. You can, however, use different SocketManager objects in 
//! different threads. Some later version could create a thread-safe 
//! socket manager.
ETWORK_API ISocketManager * CreateEtwork( EtworkSettings * settings );

//! ISocketManager is the central nervous system for the networking. 
//! Call poll() every so often with some amount of timeout in seconds (this 
//! can be 0 to make it a real poll()), and it will attempt to send data on 
//! ready-to-send sockets, receive data on sockets that have data incoming, 
//! and accept new clients when they attempt to connect.
class ISocketManager {
  public:
    //! Service this instance of the network subsystem. You must call this at 
    //! somewhat regular intervals, depending on how good latency you need 
    //! in your system.
    //! @param seconds The number of seconds to wait within select() if there 
    //! is no activity. A typical value for an action game might be 0.016 for 
    //! a nominal server frame rate of 60 Hz.
    //! @param outActive A pointer to an array of ISocket pointers. This 
    //! array will be filled in with sockets that have received data within 
    //! this call to poll. You can then service them as you see fit. It is 
    //! a good idea to drain their input fully before calling poll() again.
    //! Non-active slots will be set to NULL. This parameter must always 
    //! point to at least one socket pointer slot.
    //! @param maxActive the size (in pointers) of outActive. At most this 
    //! number of sockets will be returned as active in a single call to 
    //! poll(). It is a good idea to make this at least one greater than 
    //! the number of sockets you believe could be active, to avoid early 
    //! returns inside poll() because of filling up the active array. This 
    //! parameter must always have a value of at least 1.
    //! @return The number of sockets actually put into outActive, or -1 for 
    //! failure.
    //! @note poll() can return before the timeout has expired, if there is 
    //! activity on some socket.
    virtual int poll( double seconds, ISocket ** outActive, int maxActive ) = 0;
    //! Accept new connections from the socket manager, that have arrived during 
    //! a previous call to poll().
    //! @param outAccepted A pointer to an array of socket pointers. This array 
    //! will receive the accepted clients (if any); other slots will be set to 
    //! NULL.
    //! @param maxAccepted The maximum number of accepted clients that will be 
    //! put into the array outAccepted.
    //! @return The actual number of sockets put into outAccepted during this 
    //! call, or -1 for failure.
    virtual int accept( ISocket ** outAccepted, int maxAccepted ) = 0;
    //! Connect to a remote service. 
    //! Resolution of the host name is blocking, and thus may take a little 
    //! while; it is not suitable for use during the real-time loop of your 
    //! application. Typically, you create connections as part of initialization, 
    //! and drop them as part of shut-down.
    //! @param address The host name (some.host.com) or IP address (12.34.56.78) 
    //! of the remote host to connect to.
    //! @param port The port number (e g 80 for HTTP) that the remote host is 
    //! listening to.
    //! @param outConnected A pointer to a socket pointer. This will be set to 
    //! a newly created socket object on success, or to NULL on failure.
    //! @return -1 on failure, 1 on success. On failure, the error can be found 
    //! by using the IErrorNotify.
    //! @note This call may use blocking name resolution 
    //! if the address parameter isn't a numeric-form IP address.
    virtual int connect( char const * address, unsigned short port, ISocket ** outConnected ) = 0;
    //! When you're all done with all sockets, call dispose() to close the 
    //! listening socket (if server), and deallocate all memory used by this 
    //! networking subsystem. It is illegal to call dispose() when there are 
    //! still "live" ISockets outstanding (i e, you must dispose all of those 
    //! first).
    virtual void dispose() = 0;

  protected:
    ~ISocketManager() {}
};

//! ISocket represents a connection to a single remote host.
//! ISocket will send and receive "framed" messages (messages of a 
//! specific size), which means that there's a little bit of extra 
//! data in the TCP stream for reliable connections.
class ISocket {
  public:
    //! @return The IP address of the remote host.
    virtual sockaddr_in address() = 0;
    //! Read out one message from the socket (there may be more).
    //! If maxSize is less than the message size, then the rest 
    //! of that particular message is lost.
    //! @param buffer The buffer to receive the message into.
    //! @param maxSize The size of the buffer; at most this many 
    //! bytes of the message will be received.
    //! @return The number of bytes received; 0 if there are no 
    //! messages pending, or -1 if there was a socket error.
    //! @note If you get a packet of 0 bytes, that is an "idle" 
    //! packet, used to avoid time-outs. Such packets may be 
    //! generated by the networking layer, and may be queued by 
    //! you as well.
    virtual int read( void * buffer, size_t maxSize ) = 0;
    //! Queue a message to the other end of the connection. For 
    //! each call to write(), read() will return the same number 
    //! of bytes -- i e, the socket is packet-semantic, not 
    //! stream-semantic.
    //! @param buffer A pointer to the data to send.
    //! @param size The size of the data to send.
    //! @return The number of bytes actually sent, or 0 if there 
    //! is no queuing space, or -1 if there is a socket error.
    //! @note The message may not make it to the other side, or 
    //! may make it out of order, or may make it in more than one 
    //! copy, if you created the ISocketManager without using 
    //! the reliable flag.
    virtual int write( void const * buffer, size_t size ) = 0;
    //! Check whether the other end has closed the connection.
    //! @return True if the other end has closed the connection 
    //! (or, in the case of UDP, has timed out).
    virtual bool closed() = 0;
    //! Let go of the socket. You must dispose all sockets before 
    //! you dispose the network subsystem itself.
    virtual void dispose() = 0;

    //! While somewhat unconventional for an abstract interface, it 
    //! is convenient for the user of a socket to be able to put 
    //! some data association with the socket. The "data_" member 
    //! is free to use by any client of the ISocket; it's not used 
    //! internally by the socket or the socket manager.
    void * data_;

  protected:
    ~ISocket() {}
};

//! @}

//! \addtogroup Support Support capabilities
//! @{

//! \c operator==() and \c operator<() allow you to put \c sockaddr_in in a \c map<> or \c set<> .
inline bool operator==( sockaddr_in const & a, sockaddr_in const & b ) {
  return a.sin_family == b.sin_family && a.sin_addr.S_un.S_addr == b.sin_addr.S_un.S_addr &&
      a.sin_port == b.sin_port;
}
//! \c operator==() and \c operator<() allow you to put \c sockaddr_in in a \c map<> or \c set<> .
inline bool operator<( sockaddr_in const & a, sockaddr_in const & b ) {
  return (a.sin_family < b.sin_family) ||
      ((a.sin_family == b.sin_family) &&
        ((a.sin_addr.S_un.S_addr < b.sin_addr.S_un.S_addr) || 
          ((a.sin_addr.S_un.S_addr == b.sin_addr.S_un.S_addr) &&
            (a.sin_port < b.sin_port))));
}

//! @}

#endif  //  etwork_etwork_h


