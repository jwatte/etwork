
#include "etwork/buffer.h"

#include <string.h>
#include <deque>
#include <assert.h>

using namespace etwork;

//! The framing protocol for etwork::Buffer is simple: each packet is 
//! preceded by a two-byte length, in network-endian order.
//! If random data is received, this may result in arbitrary packet 
//! sizes; the buffer implementation is robust enough to ignore such 
//! data until an apparently well-framed message is encountered.
//!
//! You can construct a special sync sequence that is guaranteed to 
//! sync up any un-synced buffer. It consists of 65536 0-bytes (to 
//! make sure the buffer is in the mode of reading 0-length packets) 
//! followed by two 1-bytes (which will give a packet length of 1 with 
//! a 1 contained, or of 257), followed by 0, 255 (for length 255, in 
//! case alignment read a 1-byte packet) and 255 bytes of arbitrary 
//! data (say, 0 again).
//!
//! It's kind-of wasteful to send 65k of data to sync up the protocol 
//! again, but this is just an observation on the feasibility, which 
//! makes me feel clever :-)
//!
//! Note that any higher-level protocol might still be confused by the 
//! packet stream returned by such a sync protocol, which makes it 
//! even less useful for real-life usage.
namespace etwork {
  class Impl {
    public:
      struct Message {
        unsigned short offset_;
        unsigned short size_;
      };
      Impl( size_t maxMsgSize, size_t queueSize, size_t maxNumMessages );
      ~Impl();

      int Impl::put_data( void const * data, size_t size );
      int Impl::put_message( void const * msg, size_t size );
      int Impl::get_data( void * oData, size_t mSize );
      int Impl::get_message( void * oData, size_t mSize );

      size_t space_used() { return written_; }
      size_t message_count() { return queue_.size(); }

      Message * new_message( size_t size );
      void delete_message( Message * msg );

      size_t maxMsgSize_;
      size_t queueSize_;
      size_t maxNumMessages_;

      std::deque< Message * > queue_;
      Message * curTarget_;
      size_t written_;
      size_t toSkip_;
      size_t tmpSize_;
  };
}

//  It is possible, inside the implementation, to calculate the maximum 
//  size of data actually needed by the queue, and allocate that up 
//  front. Then manage the data space using a FIFO cyclic order (where 
//  the FIFO has space equal to maxMsgSize added to queueSize). However, 
//  right now, I'm using operator new() because it's easier to get to 
//  work right. Optimization later!
Impl::Impl( size_t maxMsgSize, size_t queueSize, size_t maxNumMessages )
{
  maxMsgSize_ = maxMsgSize;
  queueSize_ = queueSize;
  maxNumMessages_ = maxNumMessages;

  curTarget_ = 0;
  written_ = 0;
  toSkip_ = 0;
  tmpSize_ = (size_t)-1;
}

Impl::~Impl()
{
  size_t s = queue_.size();
  for( size_t i = 0; i < s; ++i ) {
    delete_message( queue_[i] );
  }
  queue_.clear();
}

int Impl::put_data( void const * data, size_t size )
{
#if 0   //  this is useful when debugging gnarly problems
  std::string msg( "put_data() " );
  char x[20];
  msg += itoa( (int)size, x, 10 );
  msg += " bytes;";
  for( int i = 0; i < 40 && i < (int)size; ++i ) {
    msg += " $";
    msg += itoa( ((unsigned char *)data)[i], x, 16 );
  }
  msg += "\n";
  OutputDebugString( msg.c_str() );
#endif
  int total = 0;
more:
  if( !size ) {
    return total;
  }
  //  first, deal with a half-received size
  if( tmpSize_ != (size_t)-1 ) {
    assert( curTarget_ == 0 );
    assert( toSkip_ == 0 );
    tmpSize_ += *(unsigned char *)data;
    data = (char *)data + 1;
    --size;
    ++total;
  }
  //  second, if we don't have a target, get a size for it
  else if( !curTarget_ ) {
    assert( tmpSize_ == (size_t)-1 );
    if( size == 1 ) {
      //  a single byte of the two-byte length
      tmpSize_ = *(unsigned char *)data << 8;
      return total+1;
    }
    tmpSize_ = (((unsigned char *)data)[0] << 8) + ((unsigned char *)data)[1];
    size -= 2;
    data = (char *)data + 2;
    total += 2;
  }
  //  if it's time to allocate a target, do it
  if( tmpSize_ != (size_t)-1 ) {
    assert( !curTarget_ );
    curTarget_ = new_message( tmpSize_ );
    if( !curTarget_ ) {
      //OutputDebugString( "Etwork: Skipping too large message in Buffer::put_data()\n" );
      toSkip_ = tmpSize_;
    }
    tmpSize_ = (size_t)-1;
  }
  if( curTarget_ == 0 ) {
    assert( toSkip_ > 0 );
    size_t skip = toSkip_;
    if( skip > size ) {
      skip = size;
    }
    toSkip_ -= skip;
    data = (char *)data + skip;
    size -= skip;
    total += (int)skip;
    goto more;  //  try next packet, if any
  }
  assert( curTarget_ != 0 );
  assert( tmpSize_ == (size_t)-1 );
  assert( toSkip_ == 0 );
  //  now, keep filling the target
  size_t toread = curTarget_->size_-curTarget_->offset_;
  if( toread > size ) {
    toread = size;
  }
  memcpy( &curTarget_[1], data, toread );
  data = (char *)data + toread;
  size -= toread;
  total += (int)toread;
  curTarget_->offset_ += (unsigned short)toread;
  if( curTarget_->size_ == curTarget_->offset_ ) {
    queue_.push_back( curTarget_ );
    written_ += curTarget_->size_;
    curTarget_->offset_ = 0;
    curTarget_ = 0;
  }
  goto more;
}

int Impl::put_message( void const * msg, size_t size )
{
  Message * w = new_message( size );
  if( w == 0 ) return -1;
  memcpy( &w[1], msg, size );
  queue_.push_back( w );
  written_ += size;
  return (int)size;
}

int Impl::get_data( void * oData, size_t mSize )
{
  int total = 0;
  //  ensure that we get at least one byte into a 
  //  message without staying in the middle of the size.
  if( mSize < 3 ) {
    //OutputDebugString( "Etwork: mSize must be at least 3 in Buffer::get_data()" );
    return -1;
  }
  unsigned char * data = (unsigned char *)oData;
more:
  if( queue_.empty() ) return total;
  Message * w = queue_.front();
  if( w->offset_ == 0 ) {
    data[0] = (unsigned char)(w->size_ >> 8);
    data[1] = (unsigned char)w->size_;
    data += 2;
    mSize -= 2;
    total += 2;
  }
  size_t towrite = w->size_ - w->offset_;
  if( towrite > mSize ) towrite = mSize;
  memcpy( data, &w[1], towrite );
  w->offset_ += (int)towrite;
  total += (int)towrite;
  data += towrite;
  mSize -= towrite;
  if( w->offset_ == w->size_ ) {
    queue_.pop_front();
    written_ -= w->size_;
    delete_message( w );
  }
  if( mSize < 3 ) {
    return total;
  }
  goto more;
}

int Impl::get_message( void * oData, size_t mSize )
{
  if( queue_.empty() ) {
    return -1;
  }
  Message * msg = queue_.front();
  size_t copy = msg->size_;
  if( copy > mSize ) {
    return -1;
  }
  memcpy( oData, &msg[1], copy );
  written_ -= copy;
  queue_.pop_front();
  delete_message( msg );
  return (int)copy;
}

Impl::Message * Impl::new_message( size_t size )
{
  if( size > maxMsgSize_ ) {
    //OutputDebugString( "Etwork: Request for message larger than max size in Impl::new_message()\n" );
    return 0;
  }
  if( queue_.size() >= maxNumMessages_ ) {
    //OutputDebugString( "Etwork: Attempting to allocate more than allowed number of messages in Impl::new_message()\n" );
    return 0;
  }
  if( written_ + size > queueSize_ ) {
    //OutputDebugString( "Etwork: Attempting to allocate more than allowed size of message queue in Impl::new_message()\n" );
    return 0;
  }
  Message * m = (Message *)::operator new( size + sizeof( Message ) );
  m->offset_ = 0;
  m->size_ = (int)size;
  return m;
}

void Impl::delete_message( Message * msg )
{
  ::operator delete( msg );
}


Buffer::Buffer( size_t maxMsgSize, size_t queueSize, size_t maxNumMessages )
{
  assert( sizeof( short ) == 2 );
  pImpl = new Impl( maxMsgSize, queueSize, maxNumMessages );
}

Buffer::~Buffer()
{
  delete (Impl *)pImpl;
}

int Buffer::put_data( void const * data, size_t size )
{
  return ((Impl *)pImpl)->put_data( data, size );
}

int Buffer::put_message( void const * msg, size_t size )
{
  return ((Impl *)pImpl)->put_message( msg, size );
}

int Buffer::get_data( void * oData, size_t mSize )
{
  return ((Impl *)pImpl)->get_data( oData, mSize );
}

int Buffer::get_message( void * oData, size_t mSize )
{
  return ((Impl *)pImpl)->get_message( oData, mSize );
}

size_t Buffer::space_used()
{
  return ((Impl *)pImpl)->space_used();
}

size_t Buffer::message_count()
{
  return ((Impl *)pImpl)->message_count();
}
