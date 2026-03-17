//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include "xa.h"

#include <string>

namespace oxs::xa
{
   constexpr long version = 0;

   namespace xid
   {
      auto encode( const XID* xid) -> std::string;
      auto decode( std::string_view gid) -> XID;

      inline bool null( const XID& xid)
      {
         return xid.formatID == XID{}.formatID;
      }
   };
} // oxs::xa
