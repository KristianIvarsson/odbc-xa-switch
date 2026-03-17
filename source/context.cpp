//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "context.hpp"

#include <map>

namespace oxs::context
{
   namespace local
   {
      namespace
      {
         std::map< int, data> context;

         template< typename type>
         auto get( const int rmid) -> SQLHANDLE
         {
            if( const auto result = rmid ? context.find( rmid) : context.begin(); result != context.end())
               return std::get< type>( result->second);

            return SQL_NULL_HANDLE;
         }
      } //
   } // local

   bool add( const int rmid, data&& data)
   {
      return local::context.emplace( rmid, std::move( data)).second;
   }

   auto pop( const int rmid) -> data
   {
      auto nrv = std::move( local::context.at( rmid));
      local::context.erase( rmid);
      return nrv;
   }

   bool has( const int rmid)
   {
      return local::context.contains( rmid);
   }

   auto env( const int rmid) -> const odbc::henv&
   {
      return std::get< odbc::henv>( local::context.at( rmid));
   }

   auto dbc( const int rmid) -> const odbc::hdbc&
   {
      return std::get< odbc::hdbc>( local::context.at( rmid));
   }  

} // oxs::context

#include "odbc-xa-switch/context.h"

SQLHENV oxs_get_henv( int rmid)
{
   return oxs::context::local::get< oxs::odbc::henv>( rmid);
}

SQLHDBC oxs_get_hdbc( int rmid)
{
   return oxs::context::local::get< oxs::odbc::hdbc>( rmid);
}
