//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include <sql.h>

#include <utility>

namespace oxs::odbc
{
   namespace scoped
   {
      template< SQLSMALLINT type>
      class handle
      {

      private:

         SQLHANDLE m_handle{};

      public:
   
         handle( const SQLHANDLE input) 
         {
            SQLAllocHandle( type, input, &m_handle);            
         }

         ~handle() 
         { 
            if( m_handle != SQL_NULL_HANDLE) SQLFreeHandle( type, m_handle);
         }

         handle( const handle&) = delete;
         handle& operator = ( const handle&) = delete;
         handle( handle&& other) noexcept : m_handle{ std::exchange( other.m_handle, SQLHANDLE{})} {}
         handle& operator = ( handle&& other) noexcept { return std::swap( other.m_handle, this->m_handle), *this; }

         operator SQLHANDLE() const noexcept
         {
            return m_handle;
         }

         operator bool() const noexcept
         {
            return m_handle != SQL_NULL_HANDLE;
         }
      };

   } // scoped

   using henv = scoped::handle< SQL_HANDLE_ENV>;
   using hdbc = scoped::handle< SQL_HANDLE_DBC>;
   using hstmt = scoped::handle< SQL_HANDLE_STMT>;

   inline bool failure( const SQLRETURN result)
   {
      return ! SQL_SUCCEEDED( result);
   }

   namespace detail
   {
      void logging( SQLSMALLINT type, SQLHANDLE handle);
   } // detail

   template< SQLSMALLINT type>
   auto logging( const scoped::handle< type>& handle)
   {
      return detail::logging( type, handle);
   }

} // oxs::odbc
