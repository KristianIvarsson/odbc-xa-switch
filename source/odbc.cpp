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

      auto native( const SQLSMALLINT type, const SQLHANDLE handle) -> SQLINTEGER
      {
         SQLINTEGER native{};

         if( failure( SQLGetDiagRec( type, handle, 1, NULL, &native, NULL, 0, NULL)))
            std::println( stderr, "[{}] unknown diagnostic", getpid());

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

   namespace create
   {
      namespace local
      {
         namespace
         {
            auto environment() -> std::optional< henv>
            {
               henv result{ SQL_NULL_HANDLE};

               if( failure( SQLSetEnvAttr( result, SQL_ATTR_ODBC_VERSION, reinterpret_cast< SQLPOINTER>( SQL_OV_ODBC3), 0))) 
                  [[unlikely]] return logging( result), std::nullopt;

               return result;
            }
         } //
      } // local

      auto connection( std::string_view string) -> std::optional< hdbc>
      {
         static const auto environment = local::environment();

         if( ! environment)
            [[unlikely]] return std::nullopt;

         hdbc result{ *environment};
         if( failure( SQLDriverConnect( result, NULL, reinterpret_cast< SQLCHAR*>( const_cast< char*>( string.data())), SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT))) 
            [[unlikely]] return logging( result), std::nullopt;
            
         return result;
      }
   } // create

} // oxs::odbc
