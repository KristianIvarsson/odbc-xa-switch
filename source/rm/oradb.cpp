//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/rm/oradb.h"

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

namespace oxs::oradb
{
   namespace
   {
      namespace normal
      {
         using XID = XID;
      } // normal

      namespace native
      {
         struct XID
         {
            SQLINTEGER formatID;
            SQLLEN gtrid_length;
            SQLLEN bqual_length;
            char gtrid[ 64];
            char bqual[ 64];
         };
      } // native

      static_assert( sizeof( normal::XID::data) == sizeof( native::XID::gtrid) + sizeof( native::XID::bqual));

      namespace transform
      {
         auto xid( const normal::XID& value) 
         { 
            native::XID result
            {
               .formatID = static_cast< decltype( result.formatID)>( value.formatID),
               .gtrid_length = static_cast< decltype( result.gtrid_length)>( value.gtrid_length),
               .bqual_length = static_cast< decltype( result.bqual_length)>( value.bqual_length),
            };

            std::ranges::copy( xa::xid::make::gtrid( value), result.gtrid);
            std::ranges::copy( xa::xid::make::bqual( value), result.bqual);

            return result;
         }

         namespace make
         {
            auto gtrid( const native::XID& xid)
            {
               return std::span{ xid.gtrid, static_cast< std::size_t>( xid.gtrid_length)};
            }

            auto bqual( const native::XID& xid)
            {
               return std::span{ xid.bqual, static_cast< std::size_t>( xid.bqual_length)};
            }
         } // make

         auto xid( const native::XID& value)
         {
            normal::XID result
            {
               .formatID = static_cast< decltype( result.formatID)>( value.formatID),
               .gtrid_length = static_cast< decltype( result.gtrid_length)>( value.gtrid_length),
               .bqual_length = static_cast< decltype( result.bqual_length)>( value.bqual_length),
            };
            
            std::ranges::copy( make::gtrid( value), xa::xid::make::gtrid( result).begin());
            std::ranges::copy( make::bqual( value), xa::xid::make::bqual( result).begin());

            return result;
         }
      } // transform

      namespace detail
      {
         namespace bind
         {
            namespace output
            {
               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, SQLINTEGER& value) -> int
               {
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &value, 0, NULL)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }
            } // output

            namespace input
            {
               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, const SQLINTEGER& value) -> int
               {
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, const_cast< SQLINTEGER*>( &value), 0, NULL)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }

               template< std::size_t size>
               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, const std::span< const std::byte> value) -> int
               {
                  if( value.size() > size)
                     [[unlikely]] return XAER_INVAL;

                  SQLLEN length = value.size();
                  if( odbc::failure( SQLBindParameter( hstmt, ++number, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY, size, 0, const_cast< std::byte*>( value.data()), size, &length)))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

                  return XA_OK;
               }

               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, const native::XID& value) -> int
               {
                  if( value.gtrid_length > std::ssize( value.gtrid) || value.bqual_length > std::ssize( value.bqual))
                     [[unlikely]] return XAER_INVAL;

                  if( auto xaer = parameter( hstmt, number, value.formatID))
                     [[unlikely]] return xaer;
                  
                  if( auto xaer = parameter< sizeof( value.gtrid)>( hstmt, number, std::as_bytes( std::span{ value.gtrid, static_cast< std::size_t>( value.gtrid_length)})))
                     [[unlikely]] return xaer;
                  
                  if( auto xaer = parameter< sizeof( value.bqual)>( hstmt, number, std::as_bytes( std::span{ value.bqual, static_cast< std::size_t>( value.bqual_length)})))
                     [[unlikely]] return xaer;

                  return XA_OK;
               }

               auto parameter( const odbc::hstmt& hstmt, SQLUSMALLINT& number, auto& first, auto&... other) -> int
               {
                  if( auto xaer = parameter( hstmt, number, first))
                     [[unlikely]] return xaer;

                  if constexpr( sizeof...( other) == 0)
                     return XA_OK;

                  return parameter( hstmt, number, other...);
               }

            } // input

         } // bind


         auto execute( const int rmid, const std::string_view sql, const auto&... values) -> int
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            SQLINTEGER result{};

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

         auto call( const int rmid, const std::string_view sql, const XID* const xid)
         {
            assert( xid != nullptr);
            return execute( rmid, sql, transform::xid( *xid));
         }

         auto call( const int rmid, const std::string_view sql, const XID* const xid, const long flags)
         {
            assert( xid != nullptr);
            return execute( rmid, sql, transform::xid( *xid), static_cast< SQLINTEGER>( flags));
         }


      } // detail

      auto open( char* xa_info, const int rmid, const long)
      {
         return xa::open( xa_info, rmid);
      }

      auto close( char*, const int rmid, const long)
      {
         return xa::close( nullptr, rmid);
      }

      auto start( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, "SYS.DBMS_XA.XA_START(SYS.DBMS_XA_XID(?,?,?),?);", xid, flags);
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, "SYS.DBMS_XA.XA_END(SYS.DBMS_XA_XID(?,?,?),?);", xid, flags);
      }

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         static std::unordered_map< int, std::vector< native::XID>> prepared;

         if( flags & TMSTARTRSCAN)
            prepared.erase( rmid);

         if( ! prepared.contains( rmid))
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            constexpr auto sql = "SELECT FORMATID, GLOBALID, BRANCHID FROM DBA_PENDING_TRANSACTIONS";

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql)), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;


            native::XID xid;

            {
               SQLUSMALLINT number{};
               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.formatID, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, xid.gtrid, sizeof( xid.gtrid), &xid.gtrid_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, xid.bqual, sizeof( xid.bqual), &xid.bqual_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;
            }

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

               scans.push_back( xid);
            }

            prepared.emplace( rmid, std::move( scans));
         }

         auto& scans = prepared.at( rmid);

         const auto range = std::ranges::subrange{ scans.begin(), scans.begin() + static_cast< std::ptrdiff_t>( std::min( static_cast< long>( scans.size()), count))};

         std::ranges::transform( range, xids, []( const native::XID &value) { return transform::xid( value); });

         scans.erase( scans.begin(), range.end());

         if( flags & TMENDRSCAN)
            prepared.erase( rmid);

         return static_cast< int>( range.size());
      }

      auto rollback( XID* const xid, const int rmid, const long)
      {
         return detail::call( rmid, "SYS.DBMS_XA.XA_ROLLBACK(SYS.DBMS_XA_XID(?,?,?));", xid);
      }

      auto prepare( XID* const xid, const int rmid, const long)
      {
         return detail::call( rmid, "SYS.DBMS_XA.XA_PREPARE(SYS.DBMS_XA_XID(?,?,?));", xid);
      }

      auto commit( XID* const xid, const int rmid, const long flags)
      {
         const long onephase = flags & TMONEPHASE;
         return detail::call( rmid, "SYS.DBMS_XA.XA_COMMIT(SYS.DBMS_XA_XID(?,?,?),CASE WHEN ? <> 0 THEN TRUE ELSE FALSE END);", xid, onephase);
      }

      auto forget( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, "SYS.DBMS_XA.XA_FORGET(SYS.DBMS_XA_XID(?,?,?));", xid);
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
