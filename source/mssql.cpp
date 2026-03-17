//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#include "odbc-xa-switch/mssql.h"

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
         // ms have their own XID struct with different layout
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

      namespace transform
      {
         namespace detail
         {
            template<typename target>
            auto xid( const auto& value)
            {
               target result
               {
                  .formatID = static_cast< decltype( result.formatID)>( value.formatID),
                  .gtrid_length = static_cast< decltype( result.gtrid_length)>( value.gtrid_length),
                  .bqual_length = static_cast< decltype( result.bqual_length)>( value.bqual_length),
               };

               static_assert( sizeof( value.data) == sizeof( result.data));
               std::copy_n( value.data, XIDDATASIZE, result.data);

               return result;
            }
         }

         auto xid( const XID& value) { return detail::xid< XACALLPARAM::XID>( value); }
         auto xid( const XACALLPARAM::XID& value) { return detail::xid< XID>( value); }
      } // transform


      namespace detail
      {
         auto call( const int rmid, const XID* const xid, const int operation, const long flags)
         {
            assert(xid != nullptr);
            
            XACALLPARAM param
            {
               .operation = operation, 
               .xid = transform::xid( *xid),
               .flags = static_cast< decltype( param.flags)>( flags),
            };

            if( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_XA, &param, SQL_IS_POINTER)))
               return odbc::logging< SQL_HANDLE_DBC>( context::dbc( rmid)), XAER_RMERR;

            return param.status;
         }
      } // detail

      auto close( char*, const int rmid, const long)
      {
         if( oxs::context::has( rmid))
         {
            auto [ henv, hdbc] = context::pop( rmid);

            if( odbc::failure( SQLEndTran( SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK)))
               [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;

            if( odbc::failure( SQLDisconnect( hdbc)))
               [[unlikely]] return odbc::logging( hdbc), XAER_RMERR;
         }

         return XA_OK;
      }

      auto open( char* xa_info, const int rmid, const long)
      {
         if( oxs::context::has( rmid))
            close( nullptr, rmid, TMNOFLAGS);

         assert(xa_info != nullptr);

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
         return detail::call( rmid, xid, OP_START, flags);
      }

      auto end( XID* const xid, const int rmid, const long flags)
      {
         return detail::call( rmid, xid, OP_END, flags);
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

      auto recover( XID* const xids, const long count, const int rmid, const long flags) -> int
      {
         if( count < 0)
            [[unlikely]] return XAER_INVAL;

         if( count > 0 && xids == nullptr)
            [[unlikely]] return XAER_INVAL;

         const auto capacity = static_cast< std::size_t>( count) * sizeof( XACALLPARAM::XID);
         std::vector< std::byte> payload( sizeof( XACALLPARAM) + capacity);
         auto& param = *reinterpret_cast< XACALLPARAM*>( payload.data());

         param.sizeParam = payload.size();
         param.operation = OP_RECOVER;
         param.flags = flags;
         param.sizeData = capacity;

         if( odbc::failure( SQLSetConnectAttr( context::dbc( rmid), SQL_ATTR_ENLIST_IN_XA, payload.data(), SQL_IS_POINTER)))
            [[unlikely]] return odbc::logging< SQL_HANDLE_DBC>( context::dbc( rmid)), XAER_RMERR;

         if( param.status < XA_OK)
            [[unlikely]] return param.status;

         const auto returned = param.sizeReturned;

         if( returned > capacity || returned % sizeof( XACALLPARAM::XID) != 0)
            [[unlikely]] return XAER_RMERR;

         const std::span< const XACALLPARAM::XID> prepared{ 
            reinterpret_cast< const XACALLPARAM::XID*>( payload.data() + sizeof( XACALLPARAM)), 
            returned / sizeof( XACALLPARAM::XID)};

         std::ranges::transform( prepared, xids, [](const XACALLPARAM::XID &value) { return transform::xid(value); });

         return prepared.size();
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
