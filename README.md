# odbc-xa-switch

Minimal open-source ODBC XA switch foundation primarily for XATMI.

## Current Support

- Shared library: `odbcxaswitch` (platform extension: `.so` / `.dylib` / `.dll`)

- Microsoft SQL Server:
  - Name
    - `mssql_odbc_xa_switch_t`
  - Requirements
    - Microsoft ODBC Driver for SQL Server (`msodbcsql`) 17.3+ installed on target host
  - Deviations
    - None known

- PostgreSQL
  - Name
    - `pgsql_odbc_xa_switch_t`
  - Requirements
    - PostgreSQL ODBC Driver (`psqlODBC, package odbc-postgresql`) installed on target host
  - Deviations
    - Does not support TMMIGRATE/TMJOIN or TMRESUME/TMSUSPEND
    - Ignores TMONEPHASE (everything is 2PC)
    - Does not know about XA_RDONLY
  - Note
    - `max_prepared_transactions` must be set (a good idea could be equal to `max_connections`)

- Oracle Database
  - Name
    - `oradb_odbc_xa_switch_t`
  - Requirements
    - Oracle Instant Client ODBC installed and registered with (typically `Oracle 23 ODBC driver`)
  - Deviations
    - None known

### Note

Only single threaded usage (as per ODBC)

## Build

- C++23 compiler (GCC/Clang)
- CMake 3.16+
- ODBC driver manager development headers and library
  - Linux/macOS: usually unixODBC
  - Windows: ODBC SDK/Driver Manager

```bash
cmake -S . -B build
cmake --build build
```

## OPENINFO example

`xa_info` (`OPENINFO`) is passed to `xa_open_entry` and used as an ODBC connection string.

```text
DRIVER={ODBC Driver 18 for SQL Server};SERVER=tcp:127.0.0.1,1433;DATABASE=master;UID=sa;PWD=pw;Encrypt=no;TrustServerCertificate=yes;
```
```text
Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Database=master;Uid=sa;Pwd=pw!;
```
```text
Driver={Oracle 23 ODBC driver};DBQ=localhost:1521/FREEPDB1;UID=user;PWD=pw!;
```
```text
DSN=MyDSN;
```


## XATMI service sample

```c
#include <sql.h>
#include <sqlext.h>
#include <odbc-xa-switch/context.h>

void MY_SERVICE( TPSVCINFO* svc)
{
  SQLHDBC hdbc = SQL_NULL_HDBC;

  /* rmid == 0 selects the first available connection/context */
  hdbc = oxs_get_hdbc( 0);

  if( hdbc == SQL_NULL_HDBC)
    tpreturn( TPFAIL, 0, 0, 0, 0);

  SQLHSTMT stmt = SQL_NULL_HSTMT;
  if( SQLAllocHandle( SQL_HANDLE_STMT, hdbc, &stmt) == SQL_SUCCESS)
  {
    SQLExecDirect( stmt, (SQLCHAR*)"SELECT 1", SQL_NTS);
    SQLFreeHandle( SQL_HANDLE_STMT, stmt);
  }

  tpreturn( TPSUCCESS, 0, 0, 0, 0);
}
```
