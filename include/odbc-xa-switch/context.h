//
// Copyright (c) 2026 Kristian Ivarsson
//
// Licensed under the MIT License. See https://opensource.org/licenses/MIT for details.
//

#pragma once

#include <sqltypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gets ODBC XA context connection handle for a resource manager id.
 *
 * @param rmid Resource manager identifier
 * @note If zero the first potential connection is returned
 * @return SQLHDBC or SQL_NULL_HDBC
 */
SQLHDBC oxs_get_dbc( int rmid);

#ifdef __cplusplus
}
#endif
