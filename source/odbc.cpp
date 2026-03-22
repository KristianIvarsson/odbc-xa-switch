//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//


#include "odbc.hpp"

#include <sqlext.h>

#include <unistd.h>

#include <print>

namespace oxs::odbc
{
   namespace detail
   {
      auto logging( const SQLSMALLINT type, const SQLHANDLE handle) -> SQLINTEGER
      {
         SQLCHAR state[ 5 + 1] = {};
         SQLINTEGER native{};
         SQLCHAR message[ SQL_MAX_MESSAGE_LENGTH + 1] = {};
         SQLSMALLINT length = sizeof( message);

         if( failure( SQLGetDiagRec( type, handle, 1, state, &native, message, length, &length)))
            std::println( stderr, "[{}] unknown diagnostic", getpid());
         else
            std::println( stderr, "[{}] {} [{}]", getpid(), reinterpret_cast< const char*>( message), reinterpret_cast< const char*>( state));

         return native;
      }
   } // detail

   henv::~henv() = default;

   hdbc::~hdbc()
   {
      if( *this)
         if( failure( SQLDisconnect( *this)))
            logging( *this);
   }

   namespace context
   {
      auto create( std::string_view string) -> std::optional< std::tuple< henv, hdbc>>
      {
         henv env{ SQL_NULL_HANDLE};

         if( failure( SQLSetEnvAttr( env, SQL_ATTR_ODBC_VERSION, reinterpret_cast< SQLPOINTER>( SQL_OV_ODBC3), 0))) 
            [[unlikely]] return logging( env), std::nullopt;

         hdbc dbc{ env};

         if( failure( SQLDriverConnect( dbc, NULL, reinterpret_cast< SQLCHAR*>( const_cast< char*>( string.data())), SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT))) 
            [[unlikely]] return logging( dbc), std::nullopt;

         return std::make_tuple( std::move( env), std::move( dbc));
      }
   } // context

} // oxs::odbc
