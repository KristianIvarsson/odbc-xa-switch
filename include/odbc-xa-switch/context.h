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
 * Gets ODBC XA context handles for a resource manager id.
 *
 * @param rmid Resource manager identifier
 * @note If zero the first potential context is returned
 * @return SQLHENV or SQL_NULL_HENV
 */
SQLHENV oxs_get_henv( int rmid);

/**
 * Gets ODBC XA context connection handle for a resource manager id.
 *
 * @param rmid Resource manager identifier
 * @note If zero the first potential context is returned
 * @return SQLHDBC or SQL_NULL_HDBC
 */
SQLHDBC oxs_get_hdbc( int rmid);

#ifdef __cplusplus
}
#endif
