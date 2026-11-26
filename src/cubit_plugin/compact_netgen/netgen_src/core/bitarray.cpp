/**************************************************************************/
/* File:   bitarray.cpp                                                   */
/* Autho: Joachim Schoeberl                                               */
/* Date:   01. Jun. 95                                                    */
/**************************************************************************/

/*
   data type BitArray
*/

#include "bitarray.hpp"
#include "archive.hpp"

namespace ngcore
{
  void BitArray :: SetSize (size_t asize)
  {
    if (size == asize) return;
    if (owns_data)
      {
        MemTraceFree(data, GetMemoryUsage());
        delete [] data;
      }

    size = asize;
    data = new unsigned char [Addr (size)+1];
    owns_data = true;
    MemTraceAlloc(data, GetMemoryUsage());
  }


  bool BitArray :: operator==(const BitArray& other) const
  {
    if(size != other.Size())
      return false;
    for(auto i : Range(size/CHAR_BIT))
      if(data[i] != other.data[i])
        return false;
    for(auto i : Range(size%CHAR_BIT))
      if(Test(i + CHAR_BIT * (size/CHAR_BIT)) != other.Test(i + CHAR_BIT * (size/CHAR_BIT)))
        return false;
    return true;
  }

  BitArray & BitArray :: operator= (const BitArray & ba2)
  {
    SetSize (ba2.Size());
    if (!size)
      return *this;
    for (size_t i = 0; i <= Addr (size); i++)
      data[i] = ba2.data[i];
    return *this;
  }

  std::ostream & operator<<(std::ostream & s, const BitArray & ba)
  {
    size_t n = ba.Size();
    for (size_t i = 0; i < n; i++)
      {
        if (i % 50 == 0) s << i << ": ";
        s << int(ba[i]);
        if (i % 50 == 49) s << "\n";
      }
    s << std::flush;
    return s;
  }

} // namespace ngcore
