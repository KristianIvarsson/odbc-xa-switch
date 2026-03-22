//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "xa.hpp"

#include "context.hpp"

#include <sqlext.h>

#include <cassert>

#include <format>
#include <ranges>
#include <string>
#include <vector> 
#include <charconv>
#include <algorithm>

namespace oxs::xa
{
   namespace xid
   {
      namespace hex
      {
         auto encode( std::span< const char> bytes) -> std::string
         {
            return bytes 
               | std::views::transform( []( unsigned char byte) { return std::format( "{:02x}", byte); })
               | std::views::join
               | std::ranges::to< std::string>();
         }

         bool encode( std::span< const char> source, std::span< char> target)
         {
            if( target.size() < source.size() * 2)
               return false;

             auto out = target.begin();
             for( const auto& chunk : source | std::views::transform( []( unsigned char byte) { return std::format( "{:02x}", byte); }))
             {
                out = std::ranges::copy( chunk, out).out;
             }

             return true;
         }

         bool decode( std::span< const char> source, std::span< char> target)
         {
            if( source.size() % 2 != 0 || target.size() < source.size() / 2)
               return false;

            auto out = target.begin();
            for( auto chunk : source | std::views::chunk(2))
            {
               unsigned char byte;

               if( auto [ptr, _] = std::from_chars(chunk.data(), chunk.data() + chunk.size(), byte, 16); ptr != chunk.data() + chunk.size())
                  return false;

               *out++ = byte;
            }

            return true;
         }
      } // hex

      auto encode( const XID& value) -> std::string
      {
         return std::format( "oxs:{}:{}:{}", value.formatID, hex::encode( make::gtrid( value)), hex::encode( make::bqual( value)));
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


         auto format = []( const std::string_view data)
         {
            decltype( XID::formatID) result;
            if( auto [ ptr, ec] = std::from_chars( data.data(), data.data() + data.size(), result); ec == std::errc{} && ptr == data.data() + data.size())
               return result;
            return XID{}.formatID;
         };
            
         XID result
         { 
            .formatID = format( parts[ 1]), 
            .gtrid_length = static_cast< decltype(result.gtrid_length)>(gtrid.size() / 2), 
            .bqual_length = static_cast< decltype(result.bqual_length)>(bqual.size() / 2),
         };

         if( result.gtrid_length + result.bqual_length > XIDDATASIZE)
            return {};
         
         if( ! hex::decode( gtrid, make::gtrid( result)) || ! hex::decode( bqual, make::bqual( result)))
            return {};

         return result;
      }
   } // xid


   auto open( const char* const xa_info, const int rmid) -> int
   {
      assert( xa_info != nullptr);

      if( oxs::context::has( rmid))
         close( nullptr, rmid);

      auto context = odbc::context::create( xa_info);

      if( ! context)
         [[unlikely]] return XAER_RMFAIL;

      const auto& hdbc = std::get< odbc::hdbc>( *context);

      if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0))) 
         [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

      if( ! context::add( rmid, std::move( *context)))
         [[unlikely]] return XAER_INVAL;

      return XA_OK;
   }

   auto close( const char* const xa_info, const int rmid) -> int
   {
      if( oxs::context::has( rmid))
      {
         auto [ henv, hdbc] = context::pop( rmid);

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0))) 
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
      }

      return XA_OK;
   }

} // oxs::xa