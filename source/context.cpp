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
         std::map< int, odbc::hdbc> context;

         auto get( const int rmid) -> SQLHANDLE
         {
            if( const auto result = context.find( rmid); result != context.end())
               return result->second;

            if( rmid == 0 && ! context.empty())
               return context.begin()->second;
            
            return SQL_NULL_HANDLE;
         }
      } //
   } // local

   bool add( const int rmid, odbc::hdbc&& data)
   {
      return local::context.emplace( rmid, std::move( data)).second;
   }

   auto pop( const int rmid) -> odbc::hdbc
   {
      auto nrv = std::move( local::context.at( rmid));
      local::context.erase( rmid);
      return nrv;
   }

   bool has( const int rmid)
   {
      return local::context.contains( rmid);
   }

   auto dbc( const int rmid) -> const odbc::hdbc&
   {
      return local::context.at( rmid);
   }  

} // oxs::context

#include "odbc-xa-switch/context.h"

SQLHDBC oxs_get_dbc( const int rmid)
{
   return oxs::context::local::get( rmid);
}
