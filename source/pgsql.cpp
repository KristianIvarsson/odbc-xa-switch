//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/pgsql.h"

#include "xa.hpp"
#include "odbc.hpp"
#include "context.hpp"

#include <sql.h>
#include <sqlext.h>

#include <cassert>

#include <format>
#include <ranges>
#include <vector>
#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace oxs::pgsql
{
   namespace
   {
      namespace detail
      {
         auto execute( const int rmid, const std::string_view sql)
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql.data())), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

            return XA_OK;
         }

      } // detail

      auto close( char*, const int rmid, const long)
      {
         if( context::has( rmid))
         {
            auto [ henv, hdbc] = context::pop( rmid);

            if( odbc::failure( SQLDisconnect( hdbc)))
               [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
         }

         return XA_OK;
      }

      auto open( char* xa_info, const int rmid, const long)
      {
         if( context::has( rmid))
            close( nullptr, rmid, TMNOFLAGS);

         assert(xa_info != nullptr);

         if( xa_info == nullptr)
            [[unlikely]] return XAER_INVAL;
            
         odbc::henv henv{ SQL_NULL_HANDLE};
         
         if( odbc::failure( SQLSetEnvAttr( henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast< SQLPOINTER>( SQL_OV_ODBC3), 0))) 
            [[unlikely]] return odbc::logging( henv), XAER_RMERR;

         odbc::hdbc hdbc{ henv};

         if( odbc::failure( SQLDriverConnect( hdbc, NULL, reinterpret_cast< SQLCHAR*>( xa_info), SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT))) 
            [[unlikely]] return odbc::logging( hdbc), XAER_RMFAIL;

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_AUTOCOMMIT, reinterpret_cast< SQLPOINTER>( SQL_AUTOCOMMIT_ON), 0))) 
         {
            odbc::logging( hdbc);

            if( odbc::failure( SQLDisconnect( hdbc)))
               odbc::logging( hdbc);

            return XAER_RMERR;
         }

         if( ! context::add( rmid, { std::move( henv), std::move( hdbc)}))
            [[unlikely]] return XAER_INVAL;

         return XA_OK;
      }

      auto start( XID* const xid, const int rmid, const long flags)
      {
         if( flags & ( TMRESUME | TMJOIN))
            // not supported
            [[unlikely]] return XAER_INVAL; 

         return detail::execute( rmid, "BEGIN");
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         if( flags & ( TMSUSPEND | TMMIGRATE))
            // not supported
            [[unlikely]] return XAER_INVAL;
            
         // only prepared transactions can be used through different connections
         return detail::execute( rmid, std::format( "PREPARE TRANSACTION '{}'", xa::xid::encode( xid)));
      }

      auto rollback( XID* const xid, const int rmid, const long flags)
      {
         // since there's always a prepared transaction, this must happen regardless of TMONEPHASE
         return detail::execute( rmid, std::format( "ROLLBACK PREPARED '{}'", xa::xid::encode( xid)));
      }

      auto prepare( XID* const xid, const int rmid, const long flags)
      {
         // since there's always a prepared transaction, nothing to do here
         return XA_OK;
      }

      auto commit( XID* const xid, const int rmid, const long flags)
      {
         // since there's always a prepared transaction, this must happen regardless of TMONEPHASE
         return detail::execute( rmid, std::format( "COMMIT PREPARED '{}'", xa::xid::encode( xid)));
      }

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         static std::unordered_map< int, std::vector< std::string>> prepared;

         if( flags & TMSTARTRSCAN)
            prepared.erase( rmid);

         if( ! prepared.contains( rmid))
         {
            decltype( prepared)::mapped_type scans;

            constexpr auto sql = "SELECT gid FROM pg_prepared_xacts";

            odbc::hstmt hstmt{ context::dbc( rmid)};

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql)), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

            while( true)
            {
               if(const auto fetched = SQLFetch( hstmt))
               {
                  if( fetched == SQL_NO_DATA)
                     break;

                  if( odbc::failure( fetched))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;
               }

               char data[ SQL_MAX_MESSAGE_LENGTH]{};
               SQLLEN size{ sizeof( data)};

               if( odbc::failure( SQLGetData( hstmt, 1, SQL_C_CHAR, data, size, &size)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

               // assuming SQL_MAX_MESSAGE_LENGTH is enough to hold the entire gid
               scans.emplace_back( data, static_cast< std::size_t>( size));
            }

            prepared.emplace( rmid, std::move( scans));
         }

         auto& scans = prepared.at( rmid);

         const auto range = std::ranges::subrange{ scans.begin(), scans.begin() + static_cast< std::ptrdiff_t>( std::min( static_cast< long>( scans.size()), count))};

         std::ranges::transform( range, xids, []( const auto& gid) { return xa::xid::decode( gid); });

         scans.erase( scans.begin(), range.end());

         if( flags & TMENDRSCAN)
            prepared.erase( rmid);

         if( std::any_of( xids, xids + range.size(), []( const XID& xid) { return xa::xid::null( xid); }))
            [[unlikely]] return XAER_RMFAIL;

         return static_cast< int>( range.size());
      }

      auto forget( XID* const xid, const int rmid, const long flags)
      {
         return XA_OK;
      }

      auto complete( int*, int*, const int, const long)
      {
         return XAER_PROTO;
      }
   } //
} // oxs::pgsql

struct xa_switch_t pgsql_odbc_xa_switch_t = 
{
    .name = "pgsql_odbc_xa_switch_t",
    .flags = TMNOFLAGS,
    .version = oxs::xa::version,
    .xa_open_entry = oxs::pgsql::open,
    .xa_close_entry = oxs::pgsql::close,
    .xa_start_entry = oxs::pgsql::start,
    .xa_end_entry = oxs::pgsql::end,
    .xa_rollback_entry = oxs::pgsql::rollback,
    .xa_prepare_entry = oxs::pgsql::prepare,
    .xa_commit_entry = oxs::pgsql::commit,
    .xa_recover_entry = oxs::pgsql::recover,
    .xa_forget_entry = oxs::pgsql::forget,
    .xa_complete_entry = oxs::pgsql::complete,
};
