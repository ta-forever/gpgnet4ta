///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Copyright � NetworkDLS 2010, All rights reserved
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF 
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A 
// PARTICULAR PURPOSE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef _NSWFL_CRC32_H_
#define _NSWFL_CRC32_H_
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstddef>

namespace taflib {

    class CRC32 {

    public:
        CRC32(void);
        ~CRC32(void);

        void Initialize(void);

        unsigned int FullCRC(const unsigned char *sData, size_t iDataLength);
        void FullCRC(const unsigned char *sData, size_t iLength, unsigned int *iOutCRC) const;

        void PartialCRC(unsigned int *iCRC, const unsigned char *sData, size_t iDataLength) const;

    private:
        unsigned int Reflect(unsigned int iReflect, const char cChar);
        unsigned int iTable[256]; // CRC lookup table array.
    };

} //namespace::NSWFL
#endif
