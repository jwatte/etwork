
#if !defined( etwork_notify_h )
//! \internal header guard
#define etwork_notify_h

#include "etwork/etwork.h"

//! \addtogroup API Core API
//! @{

//! INotify is an interface you can implement yourself, 
//! and pass to SetEtworkSocketNotify() to receive call-back notifications 
//! about activity on the given ISocket. Some applications may 
//! find this structure of calls to be more natural than the 
//! 100% polled model of the "base" Etwork interface.
class INotify {
  public:
    //! Etwork will call your onNotify() function when there 
    //! has been read or write activity on the socket. If you 
    //! want to distinguish different sockets, then implement 
    //! this interface in a trampoline object, or just derive 
    //! directly from it in your per-socket socket-using class.
    //! notify() will also be called when the socket is closed, 
    //! or when it times out.
    virtual void onNotify() = 0;
};

//! SetEtworkSocketNotify() installs an INotify on a specific socket. 
//! There can only be one INotify per socket. If the socket has 
//! an INotify, that interface will be called, instead of the 
//! socket being returned through the arguments to 
//! ISocketManager::poll(). ISocketManager::poll() must always be 
//! passed at least one possible output socket pointer, even if all 
//! connected sockets have a notify interface.
//! \param socket is the socket to install the INotify on. This 
//! socket must previously have been returned by 
//! ISocketManager::accept() or ISocketManager::connect().
//! \param notify is the INotify interface, or NULL to remove 
//! the current notify.
//! \note It is an error to remove the notify of a socket while 
//! inside ISocketManager::poll().
ETWORK_API void SetEtworkSocketNotify( ISocket * socket, INotify * notify );

//! @}

#endif  //  etwork_notify_h
