//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//
// Logging-only XA switch for observing transaction manager behavior

#include "xa.h"

#include "xa.hpp"

#include <print>
#include <format>
#include <chrono>
#include <ranges>
#include <vector>
#include <fstream>
#include <string_view>

#include <unistd.h>

namespace oxs::debug
{
   namespace
   {
      namespace detail
      {

         void print( std::string what)
         {
            std::println( "{}", what);
            std::ofstream{ "debug_xa_switch.log", std::ios::app} << what << std::endl;
         }

         auto timestamp()
         {
            return std::chrono::floor<std::chrono::microseconds>( std::chrono::current_zone()->to_local( std::chrono::system_clock::now()));
         }

         auto encode( const long flags)
         {
            std::vector< std::string_view> result;

            // xa_start
            if( flags & TMJOIN)        result.push_back( "TMJOIN");
            if( flags & TMRESUME)      result.push_back( "TMRESUME");

            // xa_commit
            if( flags & TMONEPHASE)    result.push_back( "TMONEPHASE");

            // xa_recover
            if( flags & TMSTARTRSCAN)  result.push_back( "TMSTARTRSCAN");
            if( flags & TMENDRSCAN)    result.push_back( "TMENDRSCAN");
            
            // xa_end
            if( flags & TMSUSPEND)     result.push_back( "TMSUSPEND");
            if( flags & TMMIGRATE)     result.push_back( "TMMIGRATE");
            if( flags & TMSUCCESS)     result.push_back( "TMSUCCESS");
            if( flags & TMFAIL)        result.push_back( "TMFAIL");      

            return std::format( "[{}]", std::views::all( result) | std::views::join_with( std::string_view{"|"}) | std::ranges::to< std::string>());      
         }

         int debug( const char* function, const char* xa_info, int rmid, long flags)
         {
            print( std::format( "[{}] [PID:{}] rmid:{} {}(xa_info='{}', flags={})",
               timestamp(), getpid(), rmid, function, xa_info ? xa_info : "NULL", encode( flags)));
            
            return XA_OK;
         }

         int debug( const char* function, const XID* xid, int rmid, long flags)
         {
            print( std::format( "[{}] [PID:{}] rmid:{} {}(xid='{}', flags={})",
               timestamp(), getpid(), rmid, function, xid ? oxs::xa::xid::encode( *xid) : "NULL", encode( flags)));
            
            return XA_OK;
         }
      } // detail

      int open( char* xa_info, int rmid, long flags)
      {
         return detail::debug( "xa_open", xa_info, rmid, flags);
      }

      int close(char* xa_info, int rmid, long flags)
      {
         return detail::debug( "xa_close", xa_info, rmid, flags);
      }

      int start(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_start", xid, rmid, flags);
      }

      int end(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_end", xid, rmid, flags);
      }

      int rollback(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_rollback", xid, rmid, flags);
      }

      int prepare(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_prepare", xid, rmid, flags);
      }

      int commit(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_commit", xid, rmid, flags);
      }

      int recover(XID* xids, long count, int rmid, long flags)
      {
         std::println( "[{}] [PID:{}] rmid:{} xa_recover(count={}, rmid={}, flags={})",
            detail::timestamp(), getpid(), rmid, count, rmid, detail::encode( flags));
         return 0; // no transactions to recover
      }

      int forget(XID* xid, int rmid, long flags)
      {
         return detail::debug( "xa_forget", xid, rmid, flags);
      }

      int complete(int* handle, int* retval, int rmid, long flags)
      {
         std::println( "[{}] [PID:{}] rmid:{} xa_complete(rmid={}, flags={})",
            detail::timestamp(), getpid(), rmid, rmid, detail::encode( flags));
         return XAER_PROTO; // async not supported
      }
   }
} // namespace oxs::debug

struct xa_switch_t debug_xa_switch = 
{
    .name = "debug_xa_switch",
    .flags = TMNOFLAGS,
    .version = 0,
    .xa_open_entry = oxs::debug::open,
    .xa_close_entry = oxs::debug::close,
    .xa_start_entry = oxs::debug::start,
    .xa_end_entry = oxs::debug::end,
    .xa_rollback_entry = oxs::debug::rollback,
    .xa_prepare_entry = oxs::debug::prepare,
    .xa_commit_entry = oxs::debug::commit,
    .xa_recover_entry = oxs::debug::recover,
    .xa_forget_entry = oxs::debug::forget,
    .xa_complete_entry = oxs::debug::complete,
};
