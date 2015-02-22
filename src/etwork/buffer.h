#if !defined( etwork_buffer_h )
//! \internal Header guard
#define etwork_buffer_h

#include "etwork/etwork.h"

namespace etwork {
//! \addtogroup Support Support capabilities
//! @{

  //! A Buffer is a data structure that can marshal data to/from the wire 
  //! protocol of etwork (which is a network-byte-order short followed by 
  //! that much data; repeat). This class is used internally by the 
  //! Etwork implementation, and is not necessary for you to use -- but 
  //! you can use it if you wish to.
  //!
  //! After you create a Buffer, you can:
  //!
  //! - Put data with put_message and get data with get_message
  //! - Put data with put_message and get data with get_data
  //! - Put data with put_data and get data with get_message
  //!
  //! Any other combination of usage (including changing usage function 
  //! in the middle of operation) is unsupported and results in unexpected
  //! behavior.
  //!
  //! Provided you follow one of these usage patterns, Buffer will properly 
  //! deal with receiving half a header, half a message; extracting half a 
  //! message, etc. It does this by running an internal state machine on 
  //! sent and received data, which includes the concept of a partial message 
  //! (being written; not yet visible through the reading API).
  class ETWORK_API Buffer {
    public:
      //! Create a buffer to store incoming and outgoing messaging data in.
      //! \param maxMsgSize is the maximum size of an individual message that 
      //! you want to be able to add -- larger messages will be dropped
      //! (while correctly following the wire protocol).
      //! \param queueSize is the total amount of data you want to be able 
      //! to queue. This ought to be at least twice the maxMsgSize.
      //! \param maxNumMessages is the total number of messages you want to 
      //! be able to queue (regardless of their total size, which is 
      //! separately capped).
      Buffer( size_t maxMsgSize, size_t queueSize, size_t maxNumMessages );
      //! Deleting a buffer disposes all queued data.
      ~Buffer();
      //! put_data() puts formatted data into the queue. If the data describes 
      //! a format that exceeds the buffer capabilities of the buffer, the 
      //! data will be dropped on the ground (silently). -1 indicates failure.
      //! Returns the number of bytes actually taken from the buffer.
      //! \param data a pointer to the data to add to the buffer.
      //! \param size The number of bytes to add.
      //! \return -1 on error.
      int put_data( void const * data, size_t size );
      //! put_raw() puts raw data into the buffer, without adding any framing.
      //! \param data a pointer to the data to add to the buffer.
      //! \param size The number of bytes to add.
      //! \return -1 on error.
      int put_raw( void const * data, size_t size );
      //! put_message() puts a message into the queue; adding formatting to 
      //! make sure that only entire messages are sent or received. If the 
      //! size of the message is bigger than capacity, it will fail, returning 
      //! -1. Returns the number of bytes actually taken from the buffer.
      //! \param msg a pointer to the message data to add.
      //! \param size The size of the message in bytes.
      int put_message( void const * msg, size_t size );
      //! get_data() reads data from the queue, including the formatting that 
      //! ensures packets are only read in their entirety. mSize must be at 
      //! least 3 when calling this function. The number of bytes read are 
      //! returned, or -1 for failure, or 0 for an empty queue.
      //! \param oData will be written to with the dequeued data.
      //! \param mSize must be at least 3, and indicates how many bytes to read (at most).
      //! \return The number of bytes read, or -1.
      int get_data( void * oData, size_t mSize );
      //! get_message() gets the next formatted message from the buffer. 
      //! Returns -1 if mSize is smaller than the message size, or if there 
      //! are no messages to get.
      //! \param oData The message is returned here.
      //! \param mSize The maximum size message that can be returned.
      //! \return -1 on error.
      int get_message( void * oData, size_t mSize );
      //! \return the amount of data currently in the buffer 
      //! (discounting any formatting).
      size_t space_used();
      //! \return the number of messages within this buffer (not counting 
      //! any partial message).
      size_t message_count();
    private:
      void * pImpl;
  };

//! @}
}

#endif  //  etwork_buffer_h

