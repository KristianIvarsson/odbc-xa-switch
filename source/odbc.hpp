//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include <sql.h>

#include <tuple>
#include <utility>
#include <optional>
#include <string_view>

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

         operator SQLSMALLINT() const noexcept
         {
            return type;
         }

         explicit operator bool() const noexcept
         {
            return m_handle != SQL_NULL_HANDLE;
         }
      };

      using henv = scoped::handle< SQL_HANDLE_ENV>;
      using hdbc = scoped::handle< SQL_HANDLE_DBC>;
      using hstmt = scoped::handle< SQL_HANDLE_STMT>;

   } // scoped

   struct henv : public scoped::henv
   {
      using scoped::henv::henv;
      henv(henv&&) = default;
      henv& operator = (henv&&) = default;
      ~henv();
   };

   struct hdbc : public scoped::hdbc
   {
      using scoped::hdbc::hdbc;
      hdbc(hdbc&&) = default;
      hdbc& operator = (hdbc&&) = default;
      ~hdbc();
   };

   using hstmt = scoped::hstmt;

   namespace context
   {
      auto create( std::string_view string) -> std::optional< std::tuple< henv, hdbc>>;
   } // context


   inline bool failure( const SQLRETURN result)
   {
      return ! SQL_SUCCEEDED( result);
   }

   namespace detail
   {
      auto logging( SQLSMALLINT type, SQLHANDLE handle) -> SQLINTEGER;
   } // detail

   template< SQLSMALLINT type>
   auto logging( const scoped::handle< type>& handle)
   {
      return detail::logging( type, handle);
   }

} // oxs::odbc
