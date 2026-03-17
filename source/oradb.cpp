//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/oradb.h"

#include "xa.hpp"
#include "odbc.hpp"
#include "context.hpp"

#include <sql.h>
#include <sqlext.h>

#include <cassert>

#include <span>
#include <format>
#include <ranges>
#include <vector>
#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace oxs::oradb
{
   namespace
   {
      namespace detail
      {
         constexpr SQLULEN max_id_size = 64;

         namespace bind
         {
            namespace output
            {
               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, long& result) -> int
               {
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &result, 0, NULL)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }
            } // output

            namespace input
            {
               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, const long& value) -> int
               {
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, const_cast< long*>( &value), 0, NULL)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }

               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, std::span< char> value) -> int
               {
                  if( value.size() > max_id_size)
                     return XAER_INVAL;

                  SQLLEN size = static_cast< SQLLEN>( value.size());
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY, max_id_size, 0, value.data(), size, &size)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }

               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, XID* const xid) -> int
               {
                  assert( xid != nullptr);

                  if( auto xaer = parameter( hstmt, number, xid->formatID))
                     return xaer;
                  
                  if( auto xaer = parameter( hstmt, number, std::span{ xid->data, static_cast< std::size_t>( xid->gtrid_length)}))
                     return xaer;

                  if( auto xaer = parameter( hstmt, number, std::span{ xid->data + xid->gtrid_length, static_cast< std::size_t>( xid->bqual_length)}))
                     return xaer;

                  return XA_OK;
               }

               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, auto& first, auto&... other) -> int
               {
                  if( auto xaer = parameter( hstmt, number, first))
                     return xaer;

                  if constexpr( sizeof...( other) == 0)
                     return XA_OK;

                  return parameter( hstmt, number, other...);
               }

            } // input

         } // bind


         auto execute( const int rmid, const std::string_view sql, auto&... values) -> int
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            long result{};

            {
               SQLUSMALLINT number{};

               if( auto xaer = detail::bind::output::parameter( hstmt, number, result))
                  [[unlikely]] return xaer;

               if( auto xaer = detail::bind::input::parameter( hstmt, number, values...))
                  [[unlikely]] return xaer;
            }

            auto pl = std::format( "BEGIN ? := {} END;", sql);

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( pl.data()), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

            return result;
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

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_AUTOCOMMIT, reinterpret_cast< SQLPOINTER>( SQL_AUTOCOMMIT_OFF), 0))) 
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
         return detail::execute( rmid, "SYS.DBMS_XA.XA_START(SYS.DBMS_XA_XID(?,?,?),?);", xid, flags);
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         return detail::execute( rmid, "SYS.DBMS_XA.XA_END(SYS.DBMS_XA_XID(?,?,?),?);", xid, flags);
      }

      auto rollback( XID* const xid, const int rmid, const long)
      {

         return detail::execute( rmid, "SYS.DBMS_XA.XA_ROLLBACK(SYS.DBMS_XA_XID(?,?,?));", xid);
      }

      auto prepare( XID* const xid, const int rmid, const long)
      {
         return detail::execute( rmid, "SYS.DBMS_XA.XA_PREPARE(SYS.DBMS_XA_XID(?,?,?));", xid);
      }

      auto commit( XID* const xid, const int rmid, const long flags)
      {
         const long onephase = flags & TMONEPHASE;
         return detail::execute( rmid, "SYS.DBMS_XA.XA_COMMIT(SYS.DBMS_XA_XID(?,?,?),CASE WHEN ? <> 0 THEN TRUE ELSE FALSE END);", xid, onephase);
      }

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         static std::unordered_map< int, std::vector< XID>> prepared;

         if( flags & TMSTARTRSCAN)
            prepared.erase( rmid);

         if( ! prepared.contains( rmid))
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            
            XID xid{};

            {
               auto gtrid = xid.data;
               auto bqual = xid.data + detail::max_id_size;

               SQLUSMALLINT number{};
               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.formatID, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, gtrid, detail::max_id_size, &xid.gtrid_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, bqual, detail::max_id_size, &xid.bqual_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;
            }

            constexpr auto sql = "SELECT FORMATID, GLOBALID, BRANCHID FROM DBA_PENDING_TRANSACTIONS";

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql)), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

            decltype( prepared)::mapped_type scans;

            while( true)
            {
               if(const auto fetched = SQLFetch( hstmt))
               {
                  if( fetched == SQL_NO_DATA)
                     break;

                  if( odbc::failure( fetched))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;
               }

               if( static_cast< std::size_t>( xid.gtrid_length + xid.bqual_length) > sizeof( xid.data))
                  [[unlikely]] return XAER_RMFAIL;

               std::copy_n( xid.data + detail::max_id_size, xid.bqual_length, xid.data + xid.gtrid_length);

               scans.push_back( std::move( xid));
            }

            prepared.emplace( rmid, std::move( scans));
         }

         auto& scans = prepared.at( rmid);

         const auto range = std::ranges::subrange{ scans.begin(), scans.begin() + static_cast< std::ptrdiff_t>( std::min( static_cast< long>( scans.size()), count))};

         std::ranges::copy( range, xids);

         scans.erase( scans.begin(), range.end());

         if( flags & TMENDRSCAN)
            prepared.erase( rmid);

         return static_cast< int>( range.size());
      }

      auto forget( XID* const xid, const int rmid, const long flags)
      {
         return detail::execute( rmid, "SYS.DBMS_XA.XA_FORGET(SYS.DBMS_XA_XID(?,?,?));", xid);
      }

      auto complete( int*, int*, const int, const long)
      {
         return XAER_PROTO;
      }
   } //
} // oxs::oradb

struct xa_switch_t oradb_odbc_xa_switch_t = 
{
    .name = "oradb_odbc_xa_switch_t",
    .flags = TMNOFLAGS,
    .version = oxs::xa::version,
    .xa_open_entry = oxs::oradb::open,
    .xa_close_entry = oxs::oradb::close,
    .xa_start_entry = oxs::oradb::start,
    .xa_end_entry = oxs::oradb::end,
    .xa_rollback_entry = oxs::oradb::rollback,
    .xa_prepare_entry = oxs::oradb::prepare,
    .xa_commit_entry = oxs::oradb::commit,
    .xa_recover_entry = oxs::oradb::recover,
    .xa_forget_entry = oxs::oradb::forget,
    .xa_complete_entry = oxs::oradb::complete,
};
