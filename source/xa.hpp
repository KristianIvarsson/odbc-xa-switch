//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include "xa.h"

#include <span>
#include <string>

namespace oxs::xa
{
   constexpr long version = 0;

   namespace xid
   {
      namespace hex
      {
         auto encode( std::span< const char> bytes) -> std::string;

         bool encode( std::span< const char> source, std::span< char> target);
         bool decode( std::span< const char> source, std::span< char> target);
      } // hex

      namespace make
      {
         auto gtrid( auto& xid)
         {
            return std::span{ xid.data, static_cast< std::size_t>( xid.gtrid_length)};
         }

         auto bqual( auto& xid)
         {
            return std::span{ xid.data + xid.gtrid_length, static_cast< std::size_t>( xid.bqual_length)};
         }
      } // make

      auto encode( const XID& xid) -> std::string;
      auto decode( std::string_view gid) -> XID;

      inline bool null( const XID& xid)
      {
         return xid.formatID == XID{}.formatID;
      }
   } // xid


   auto open( const char* xa_info, int rmid) -> int;
   auto close( const char* xa_info, int rmid) -> int;

} // oxs::xa
