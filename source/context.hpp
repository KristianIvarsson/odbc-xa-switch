//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include "odbc.hpp"

namespace oxs
{
   namespace context
   {
      bool add( int rmid, odbc::hdbc&& data);
      auto pop( int rmid) -> odbc::hdbc;
      bool has( int rmid);

      auto dbc( int rmid) -> const odbc::hdbc&;
   } // context
} // oxs
