//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc.hpp"

#include <unistd.h>

#include <print>

namespace oxs::odbc
{
   namespace detail
   {
      void logging( const SQLSMALLINT type, const SQLHANDLE handle)
      {
         SQLCHAR state[ 5 + 1] = {};
         SQLCHAR message[ SQL_MAX_MESSAGE_LENGTH + 1] = {};
         SQLSMALLINT length = sizeof( message);

         if( failure( SQLGetDiagRec( type, handle, 1, state, nullptr, message, length, &length)))
            std::println( stderr, "[{}] unknown diagnostic", getpid());
         else
            std::println( stderr, "[{}] {} [{}]", getpid(), reinterpret_cast< const char*>( message), reinterpret_cast< const char*>( state));
      }
   } // detail

} // oxs::odbc
