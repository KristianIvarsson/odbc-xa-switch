//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "xa.hpp"


#include <format>
#include <ranges>
#include <string>
#include <vector> 
#include <charconv>
#include <algorithm>

#include <cassert>

namespace oxs::xa
{
   namespace xid
   {
      namespace local
      {
         namespace
         {
            auto format( const std::string_view data)
            {
               decltype( XID::formatID) result;
               if( auto [ ptr, ec] = std::from_chars( data.data(), data.data() + data.size(), result); ec == std::errc{} && ptr == data.data() + data.size())
                  return result;
               return XID{}.formatID;
            }

            auto gtrid( auto& xid)
            {
               return std::span{ xid.data, static_cast< std::size_t>( xid.gtrid_length)};
            }

            auto bqual( auto& xid)
            {
               return std::span{ xid.data + xid.gtrid_length, static_cast< std::size_t>( xid.bqual_length)};
            }
         } //
      } //

      auto encode( const XID& value) -> std::string
      {
         auto transform = []( auto bytes)
         {
            return bytes 
               | std::views::transform( []( unsigned char byte) { return std::format( "{:02x}", byte); })
               | std::views::join
               | std::ranges::to< std::string>();
         };

         return std::format( "oxs:{}:{}:{}", value.formatID, transform( local::gtrid( value)), transform( local::bqual( value)));
      }

      auto encode( const XID* const xid) -> std::string
      {
         assert( xid != nullptr);
         return encode( *xid);
      }

      auto decode( const std::string_view value) -> XID
      {
         const auto parts = value 
            | std::views::split(':') 
            | std::views::transform( []( auto part) { return std::string_view{ part.data(), part.size()}; }) 
            | std::ranges::to< std::vector>();

         if( parts.size() != 4 || parts[ 0] != "oxs")
            return {};

         const auto& gtrid = parts[ 2];
         const auto& bqual = parts[ 3];

         if( gtrid.size() % 2 || bqual.size() % 2)
            return {};
         
         XID result
         { 
            .formatID = local::format( parts[ 1]), 
            .gtrid_length = static_cast< decltype(result.gtrid_length)>(gtrid.size() / 2), 
            .bqual_length = static_cast< decltype(result.bqual_length)>(bqual.size() / 2),
         };

         if( result.gtrid_length + result.bqual_length > XIDDATASIZE)
            return {};
         
         auto transform = []( auto hex, auto out)
         {
            for( std::size_t i = 0; i < hex.size(); i += 2)
            {
               unsigned char byte;
               const auto first = hex.data() + i;

               if( auto [ ptr, _] = std::from_chars( first, first + 2, byte, 16); ptr != first + 2)
                  return false;

               out[ i / 2] = byte;
            }

            return true;
         };

         if( ! transform( gtrid, local::gtrid( result)) || ! transform( bqual, local::bqual( result)))
            return {};

         return result;
      }

   } // xid
} // oxs::xa