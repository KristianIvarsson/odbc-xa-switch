//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/rm/maria.h"

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

namespace oxs::maria
{
   namespace
   {
      constexpr int ER_XAER_NOTA = 1397;
      constexpr int ER_XAER_INVAL = 1398;
      constexpr int ER_XAER_RMFAIL = 1399;
      constexpr int ER_XAER_OUTSIDE = 1400;
      constexpr int ER_XAER_RMERR = 1401;
      constexpr int ER_XA_RBROLLBACK = 1402;
      constexpr int ER_XAER_DUPID = 1440;
      constexpr int ER_XA_RBDEADLOCK = 1613;
      constexpr int ER_XA_RBTIMEOUT = 1614;
      
      namespace detail
      {
         namespace native
         {
            auto logging( const odbc::hstmt& hstmt) -> int
            {
               switch( odbc::logging( hstmt))
               {
                  case ER_XAER_NOTA:      return XAER_NOTA;
                  case ER_XAER_INVAL:     return XAER_INVAL;
                  case ER_XAER_RMFAIL:    return XAER_RMFAIL;
                  case ER_XAER_OUTSIDE:   return XAER_OUTSIDE;
                  case ER_XAER_RMERR:     return XAER_RMERR;
                  case ER_XA_RBROLLBACK:  return XA_RBROLLBACK;
                  case ER_XAER_DUPID:     return XAER_DUPID;
                  case ER_XA_RBDEADLOCK:  return XA_RBDEADLOCK;
                  case ER_XA_RBTIMEOUT:   return XA_RBTIMEOUT;
                  default:                return XAER_RMFAIL;
               }
            }

            namespace xid
            {
               auto encode( const XID* const value)
               {
                  assert( value != nullptr);

                  auto gtrid = [] ( const auto& xid) { return xa::xid::hex::encode( xa::xid::make::gtrid( xid)); };
                  auto bqual = [] ( const auto& xid) { return xa::xid::hex::encode( xa::xid::make::bqual( xid)); };
                  return std::format( "X'{}',X'{}',{}", gtrid( *value), bqual( *value), value->formatID);
               }
            } // xid
         } // native

         auto execute( const int rmid, std::string_view sql)
         {
            odbc::hstmt hstmt{ context::dbc( rmid)};

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql.data())), SQL_NTS)))
               [[unlikely]] return native::logging( hstmt);

            return XA_OK;
         }

         auto execute( const int rmid, const std::string_view entry, const XID* const xid)
         {
            return execute( rmid, std::format( "{} {}", entry, detail::native::xid::encode( xid)));
         }

      } // detail

      auto open( char* info, const int rmid, const long)
      {
         return xa::open( info, rmid);
      }

      auto close( char* info, const int rmid, const long)
      {
         return xa::close( info, rmid);
      }

      auto start( XID* const xid, const int rmid, const long flags)
      {
         if( flags & ( TMJOIN))
            // not supported
            [[unlikely]] return XAER_INVAL; 

         if( flags & ( TMRESUME))
            return XA_OK; 

         return detail::execute( rmid, "XA START", xid);
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         if( flags & ( TMMIGRATE))
            // not supported
            [[unlikely]] return XAER_INVAL;
            
         if( flags & ( TMSUSPEND))
            return XA_OK;

         // only prepared transactions can be used through different connections
         return detail::execute( rmid, "XA END", xid) | detail::execute( rmid, "XA PREPARE", xid);
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

            constexpr auto sql = "XA RECOVER";

            if( odbc::failure( SQLExecDirect( hstmt, reinterpret_cast< SQLCHAR*>( const_cast< char*>( sql)), SQL_NTS)))
               [[unlikely]] return detail::native::logging( hstmt);


            XID xid;

            {
               SQLUSMALLINT number{};
               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.formatID, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.gtrid_length, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_LONG, &xid.bqual_length, 0, NULL)))
                  [[unlikely]] return odbc::logging( hstmt), XAER_RMERR;

               if( odbc::failure( SQLBindCol( hstmt, ++number, SQL_C_BINARY, xid.data, sizeof( xid.data), NULL)))
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
                     [[unlikely]] return detail::native::logging( hstmt);
               }

               scans.push_back( xid);
            }

            prepared.emplace( rmid, std::move( scans));
         }

         auto& scans = prepared.at( rmid);

         const auto range = std::ranges::subrange{ scans.begin(), scans.begin() + std::min( static_cast< long>( scans.size()), count)};

         std::ranges::copy( range, xids);

         scans.erase( scans.begin(), range.end());

         if( flags & TMENDRSCAN)
            prepared.erase( rmid);

         return static_cast< int>( range.size());
      }

      auto rollback( XID* const xid, const int rmid, const long )
      {
         return detail::execute( rmid, "XA ROLLBACK", xid);
      }

      auto prepare( XID* const, const int , const long )
      {
         // there's always a prepared transaction
         return XA_OK;
      }

      auto commit( XID* const xid, const int rmid, const long )
      {
         return detail::execute( rmid, "XA COMMIT", xid);
      }

      auto forget( XID* const, const int , const long )
      {
         return XA_OK;
      }

      auto complete( int*, int*, const int, const long)
      {
         return XAER_PROTO;
      }
   } //
} // oxs::maria

struct xa_switch_t maria_odbc_xa_switch_t = 
{
    .name = "maria_odbc_xa_switch_t",
    .flags = TMNOMIGRATE,
    .version = oxs::xa::version,
    .xa_open_entry = oxs::maria::open,
    .xa_close_entry = oxs::maria::close,
    .xa_start_entry = oxs::maria::start,
    .xa_end_entry = oxs::maria::end,
    .xa_rollback_entry = oxs::maria::rollback,
    .xa_prepare_entry = oxs::maria::prepare,
    .xa_commit_entry = oxs::maria::commit,
    .xa_recover_entry = oxs::maria::recover,
    .xa_forget_entry = oxs::maria::forget,
    .xa_complete_entry = oxs::maria::complete,
};
