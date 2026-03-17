//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include "odbc.hpp"

#include <tuple>

namespace oxs
{
   namespace context
   {
      using data = std::tuple< odbc::henv, odbc::hdbc>;

      bool add( int rmid, data&& data);
      auto pop( int rmid) -> data;
      bool has( int rmid);

      auto env( int rmid) -> const odbc::henv&;
      auto dbc( int rmid) -> const odbc::hdbc&;
   } // context
} // oxs
