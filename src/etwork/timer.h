#if !defined( etwork_timer_h )
//! \internal Header guard
#define etwork_timer_h

#include "etwork/etwork.h"

#if defined( WIN32 )

namespace etwork {
//! \addtogroup Support Support capabilities
//! @{

  //! A Timer is really just a clock. It measures time passed 
  //! since the timer was created, and returns it in double-precision
  //! seconds.
  class Timer {
    public:
      //! Initialize the timer; measure current baseline time.
      Timer() {
        unsigned __int64 freq;
        ::QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
        scaling_ = 1.0 / (double)freq;
        ::QueryPerformanceCounter( (LARGE_INTEGER *)&baseTime_ );
      }
      //! Return the elapsed time since construction in seconds.
      //! Returns elapsed time as a double-precision floating point number.
      double seconds() const throw() {
        unsigned __int64 cur;
        ::QueryPerformanceCounter( (LARGE_INTEGER *)&cur );
        cur -= baseTime_;
        return cur * scaling_;
      }
    private:
      //! \internal baseline timer reading
      unsigned __int64 baseTime_;
      //! \internal seconds per tick
      double scaling_;
  };

//! @}

}

#else
#error "implement me!"
#endif

#endif  //  etwork_timer_h

