//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/rm/mssql.h"

#include "xa.hpp"
#include "odbc.hpp"
#include "context.hpp"

#include <sql.h>
#include <sqlext.h>

#include <cassert>

#include <span>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace oxs::mssql
{
   namespace
   {
      constexpr int OP_START      = 0;
      constexpr int OP_END        = 1;
      constexpr int OP_PREPARE    = 2;
      constexpr int OP_COMMIT     = 3;
      constexpr int OP_ROLLBACK   = 4;
      constexpr int OP_FORGET     = 5;
      constexpr int OP_RECOVER    = 6;

      struct XACALLPARAM
      {
         struct XID
         {
            int formatID;
            int gtrid_length;
            int bqual_length;
            char data[ XIDDATASIZE];
         };

         unsigned int sizeParam{ sizeof( XACALLPARAM)};
         int operation;
         XID xid;
         int flags;
         int status;
         unsigned int sizeData;
         unsigned int sizeReturned;
      };

      namespace normal
      {
         using XID = XID;
      } // normal

      namespace native
      {
         using XID = XACALLPARAM::XID;
      } // native

      static_assert( sizeof( normal::XID::data) == sizeof( native::XID::data));

      namespace transform
      {
         namespace detail
         {
            template<typename target, typename source>
            auto xid( const source& value)
            {
               target result
               {
                  .formatID = static_cast< decltype( result.formatID)>( value.formatID),
                  .gtrid_length = static_cast< decltype( result.gtrid_length)>( value.gtrid_length),
                  .bqual_length = static_cast< decltype( result.bqual_length)>( value.bqual_length),
               };

               std::copy_n( value.data, XIDDATASIZE, result.data);

               return result;
            }
         }

         auto xid( const normal::XID& value) { return detail::xid< native::XID>( value); }
         auto xid( const native::XID& value) { return detail::xid< normal::XID>( value); }
      } // transform

      namespace detail
      {
         auto call( const int rmid, const XID* const xid, const int operation, const long flags)
         {
            assert( xid != nullptr);
            
            XACALLPARAM param
            {
               .operation = operation, 
               .xid = transform::xid( *xid),
               .flags = static_cast< decltype( param.flags)>( flags),
            };

            switch( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_XA, &param, SQL_IS_POINTER)))
            case SQL_ERROR:
            case SQL_INVALID_HANDLE:
            case SQL_STILL_EXECUTING:
               return odbc::logging( context::dbc( rmid)), XAER_RMERR;
            
            return param.status;
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
         return detail::call( rmid, xid, OP_START, flags);
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_END, flags);
      }

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         const auto capacity = static_cast< std::size_t>( count) * sizeof( XACALLPARAM::XID);

         std::vector< std::byte> payload( sizeof( XACALLPARAM) + capacity);

         XACALLPARAM param
         {
            .sizeParam = static_cast< decltype( param.sizeParam)>( payload.size()),
            .operation = OP_RECOVER,
            .flags = static_cast< decltype( param.flags)>( flags),
            .sizeData = static_cast< decltype( param.sizeData)>( capacity),
         };

         std::ranges::copy( std::as_bytes( std::span{ &param, 1}), payload.begin());

         if( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_XA, payload.data(), SQL_IS_POINTER)))
            [[unlikely]] return odbc::logging( context::dbc( rmid)), XAER_RMERR;

         std::ranges::copy( std::span{ payload}.first( sizeof( param)), std::as_writable_bytes( std::span{ &param, 1}).begin());

         if( param.status < XA_OK)
            [[unlikely]] return param.status;


         std::span data{ std::span{ payload}.subspan( sizeof( XACALLPARAM), param.sizeReturned)};
         std::vector< XACALLPARAM::XID> prepared( param.sizeReturned / sizeof( XACALLPARAM::XID));
         std::ranges::copy( data, std::as_writable_bytes( std::span{ prepared}).begin());
         std::ranges::transform( prepared, xids, []( const native::XID &value) { return transform::xid( value); });

         return static_cast< int>( prepared.size());
      }

      auto rollback( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_ROLLBACK, flags);
      }

      auto prepare( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_PREPARE, flags);
      }

      auto commit( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_COMMIT, flags);
      }

      auto forget( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_FORGET, flags);
      }

      auto complete( int*, int*, const int, const long)
      {
         return XAER_PROTO;
      }
   } //
} // oxs::mssql

struct xa_switch_t mssql_odbc_xa_switch_t = 
{
    .name = "mssql_odbc_xa_switch_t",
    .flags = TMNOFLAGS,
    .version = oxs::xa::version,
    .xa_open_entry = oxs::mssql::open,
    .xa_close_entry = oxs::mssql::close,
    .xa_start_entry = oxs::mssql::start,
    .xa_end_entry = oxs::mssql::end,
    .xa_rollback_entry = oxs::mssql::rollback,
    .xa_prepare_entry = oxs::mssql::prepare,
    .xa_commit_entry = oxs::mssql::commit,
    .xa_recover_entry = oxs::mssql::recover,
    .xa_forget_entry = oxs::mssql::forget,
    .xa_complete_entry = oxs::mssql::complete,
};
