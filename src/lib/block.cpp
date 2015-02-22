
#include "etwork/marshal.h"


Block::Block( void * base, size_t size )
{
  buf_ = (unsigned char *)base;
  size_ = size;
  pos_ = 0;
  deleteIt_ = false;
  atEof_ = false;
}

Block::Block( size_t size )
{
  buf_ = (unsigned char *)::operator new( size );
  size_ = size;
  pos_ = 0;
  deleteIt_ = true;
  atEof_ = false;
}

Block::~Block()
{
  if( deleteIt_ ) {
    ::operator delete( buf_ );
  }
}

unsigned char * Block::cur()
{
  return buf_ + pos_;
}

unsigned char const * Block::cur() const
{
  return buf_ + pos_;
}

size_t Block::left() const
{
  return size_ - pos_;
}

size_t Block::pos() const
{
  return pos_;
}

void Block::seek( size_t pos )
{
  pos_ = pos;
  atEof_ = false;
}

size_t Block::read( void * outBuf, size_t size )
{
  if( left() < size ) {
    size = left();
    if( size == 0 ) {
      atEof_ = true;
    }
  }
  memcpy( outBuf, buf_+pos_, size );
  pos_ += size;
  return size;
}

size_t Block::write( void const * inBuf, size_t size )
{
  if( left() < size ) {
    size = left();
    atEof_ = true;
  }
  memcpy( buf_+pos_, inBuf, size );
  pos_ += size;
  return size;
}

unsigned char * Block::begin()
{
  return buf_;
}

unsigned char const * Block::begin() const
{
  return buf_;
}

unsigned char * Block::end()
{
  return buf_ + size_;
}

unsigned char const * Block::end() const
{
  return buf_ + size_;
}

size_t Block::size() const
{
  return size_;
}

Block & Block::operator<<( Block const & o )
{
  write( o.begin(), o.size() );
  return *this;
}

Block & Block::operator>>( Block & o )
{
  if( o.write( begin(), size() ) < size() ) {
    atEof_ = true;
  }
  return *this;
}

bool Block::eof() const
{
  return atEof_;
}


