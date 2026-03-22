//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/rm/sqlany.h"

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

namespace oxs::sqlany
{
   namespace
   {
      constexpr int SQL_ATTR_2PC_STATE = 1210;

      constexpr SQLULEN ASA_2PC_NONE = 0;
      constexpr SQLULEN ASA_2PC_NORMAL = 0;
      constexpr SQLULEN ASA_2PC_PREPARE = 1;
      constexpr SQLULEN ASA_2PC_COMMIT = 2;
      constexpr SQLULEN ASA_2PC_ROLLBACK = 3;
      constexpr SQLULEN ASA_2PC_FORGET = 4;

      namespace normal
      {
         using XID = XID;
      } // normal

      namespace native
      {
         struct XID
         {
            long formatID;
            long gtrid_hex_length;
            long bqual_hex_length;
            char gtrid[ 128];
            char bqual[ 128];
         };
      } // native

      namespace transform
      {
         auto xid( const native::XID& value)
         {
            normal::XID result
            {
               .formatID = value.formatID,
               .gtrid_length = value.gtrid_hex_length / 2,
               .bqual_length = value.bqual_hex_length / 2,
            };
            
            xa::xid::hex::decode( value.gtrid, xa::xid::make::gtrid( result));
            xa::xid::hex::decode( value.bqual, xa::xid::make::bqual( result));

            return result;
         }
      } // transform


      auto open( char* xa_info, const int rmid, const long)
      {
         return xa::open( xa_info, rmid);
      }

      auto close( char*, const int rmid, const long)
      {
         return xa::close( nullptr, rmid);
      }

      auto start( XID* const xid, const int rmid, const long)
      {
         if( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_DTC, xid, SQL_IS_POINTER)))
            return odbc::logging( context::dbc( rmid)), XAER_RMERR;

         return XA_OK;
      }

      auto end( XID* const, const int rmid, const long)
      {
         if( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_DTC, NULL, SQL_IS_POINTER)))
            return odbc::logging( context::dbc( rmid)), XAER_RMERR;

         return XA_OK;
      }

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         static std::unordered_map< int, std::vector< normal::XID>> prepared;

         if( flags & TMSTARTRSCAN)
            prepared.erase( rmid);

         if( ! prepared.contains( rmid))
         {
            decltype( prepared)::mapped_type scans;

            constexpr auto sql = "SELECT format_id, gtrid, bqual FROM sa_transactions() WHERE state = 'Prepared'";

            odbc::hstmt hstmt{ context::dbc( rmid)};

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql)), SQL_NTS)))
               [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;

            native::XID xid;

            {
               SQLUSMALLINT number{};
               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.formatID, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, xid.gtrid, sizeof( xid.gtrid), &xid.gtrid_hex_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, xid.bqual, sizeof( xid.bqual), &xid.bqual_hex_length)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;
            }

            while( true)
            {
               if(const auto fetched = SQLFetch( hstmt))
               {
                  if( fetched == SQL_NO_DATA)
                     break;

                  if( odbc::failure( fetched))
                     [[unlikely]] return odbc::logging( hstmt), XAER_RMFAIL;
               }

               scans.push_back( transform::xid( xid));
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

      auto rollback( XID* const xid, const int rmid, const long)
      {
         const auto& hdbc = context::dbc( rmid);

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_ENLIST_IN_DTC, xid, SQL_IS_POINTER)))
            return odbc::logging( hdbc), XAER_RMERR;

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_2PC_STATE, (SQLPOINTER)ASA_2PC_ROLLBACK, 0)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
      
         if( odbc::failure( SQLEndTran( hdbc, hdbc, SQL_ROLLBACK)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
         
         return XA_OK;
      }

      auto prepare( XID* const xid, const int rmid, const long)
      {
         const auto& hdbc = context::dbc( rmid);

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_ENLIST_IN_DTC, xid, SQL_IS_POINTER)))
            return odbc::logging( hdbc), XAER_RMERR;

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_2PC_STATE, (SQLPOINTER)ASA_2PC_PREPARE, 0)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
         
         if( odbc::failure( SQLEndTran( hdbc, hdbc, SQL_COMMIT)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

         SQLPOINTER check = NULL;
         if( odbc::failure( SQLGetConnectAttr(hdbc, SQL_ATTR_ENLIST_IN_DTC, &check, 0, NULL)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

         if( check == NULL)
            return XA_RDONLY;

         return XA_OK;
      }

      auto commit( XID* const xid, const int rmid, const long flags)
      {
         const auto& hdbc = context::dbc( rmid);

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_ENLIST_IN_DTC, xid, SQL_IS_POINTER)))
            return odbc::logging( hdbc), XAER_RMERR;

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_2PC_STATE, (SQLPOINTER)(flags & TMONEPHASE ? ASA_2PC_NONE : ASA_2PC_COMMIT), 0)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
         
         if( odbc::failure( SQLEndTran( hdbc, hdbc, SQL_COMMIT)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

         return XA_OK;
      }

      auto forget( XID* const xid, const int rmid, const long)
      {
         const auto& hdbc = context::dbc( rmid);

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_ENLIST_IN_DTC, xid, SQL_IS_POINTER)))
            return odbc::logging( hdbc), XAER_RMERR;

         if( odbc::failure( SQLSetConnectAttr( hdbc, SQL_ATTR_2PC_STATE, (SQLPOINTER)ASA_2PC_FORGET, 0)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

         if( odbc::failure( SQLEndTran( hdbc, hdbc, SQL_COMMIT)))
            [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

         return XA_OK;
      }

      auto complete( int*, int*, const int, const long)
      {
         return XAER_PROTO;
      }
   } //
} // oxs::sqlany

struct xa_switch_t sqlany_odbc_xa_switch_t = 
{
    .name = "sqlany_odbc_xa_switch_t",
    .flags = TMNOFLAGS,
    .version = oxs::xa::version,
    .xa_open_entry = oxs::sqlany::open,
    .xa_close_entry = oxs::sqlany::close,
    .xa_start_entry = oxs::sqlany::start,
    .xa_end_entry = oxs::sqlany::end,
    .xa_rollback_entry = oxs::sqlany::rollback,
    .xa_prepare_entry = oxs::sqlany::prepare,
    .xa_commit_entry = oxs::sqlany::commit,
    .xa_recover_entry = oxs::sqlany::recover,
    .xa_forget_entry = oxs::sqlany::forget,
    .xa_complete_entry = oxs::sqlany::complete,
};
