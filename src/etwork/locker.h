#if !defined( etwork_locker_h )
//! \internal Header guard
#define etwork_locker_h

#if defined( WIN32 )

namespace etwork {
//! \addtogroup Support Support capabilities
//! @{

  //! A Lock is a critical section (mutual exclusion 
  //! within a single process). It can only be locked and 
  //! unlocked through the Locker class (to be exception 
  //! safe).
  class Lock {
    public:
      //! Creating a Lock will create the underlying system lock.
      Lock() {
        InitializeCriticalSection( &lock_ );
      }
      //! Destroying a Lock will unblock anyone currently waiting on it.
      ~Lock() {
        DeleteCriticalSection( &lock_ );
      }
    private:
      friend class Locker;
      CRITICAL_SECTION lock_;
  };
  //! Class Locker is intended to be created on the stack, straddling 
  //! some critical section. Using a locker class makes the mutual 
  //! exclusion exception safe.
  class Locker {
    public:
      //! Acquire the given Lock. This constructor will not return 
      //! until the lock is acquired by the current thread, or the 
      //! lock is destroyed.
      Locker( Lock & l ) : l_( l ) {
        EnterCriticalSection( &l_.lock_ );
      }
      //! Release the lock, making it available for another thread 
      //! to acquire.
      ~Locker() {
        LeaveCriticalSection( &l_.lock_ );
      }
    private:
      Lock & l_;
  };

//! @}
}

#else
#error "implement me!"
#endif

#endif  //  etwork_locker_h

