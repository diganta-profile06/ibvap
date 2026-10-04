#include "Database.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace
{

EM_JS(
    void,
    ibvap_database_prepare_storage,
    (),
    {
        if (!Module.IBVAPDatabaseStorageMounted)
        {
            try
            {
                FS.mkdir('/ibvap');
            }
            catch (error)
            {
                if (!error || error.code !== 'EEXIST')
                {
                    throw error;
                }
            }

            FS.mount(
                IDBFS,
                {},
                '/ibvap'
            );

            Module.IBVAPDatabaseStorageMounted = true;
            Module.IBVAPDatabaseStorageReady = false;

            FS.syncfs(
                true,
                function(error)
                {
                    if (error)
                    {
                        console.error(
                            'IBVAP database: IDBFS sync failed',
                            error
                        );

                        Module.IBVAPDatabaseStorageReady = false;
                        Module.IBVAPDatabaseStorageError = true;
                        return;
                    }

                    Module.IBVAPDatabaseStorageReady = true;
                    Module.IBVAPDatabaseStorageError = false;
                }
            );
        }
    }
);

EM_JS(
    int,
    ibvap_database_storage_ready,
    (),
    {
        return Module.IBVAPDatabaseStorageReady ? 1 : 0;
    }
);

EM_JS(
    int,
    ibvap_database_storage_error,
    (),
    {
        return Module.IBVAPDatabaseStorageError ? 1 : 0;
    }
);

EM_JS(
    void,
    ibvap_database_sync,
    (),
    {
        if (!Module.IBVAPDatabaseStorageMounted)
        {
            return;
        }

        FS.syncfs(
            false,
            function(error)
            {
                if (error)
                {
                    console.error(
                        'IBVAP database: IDBFS write sync failed',
                        error
                    );
                }
            }
        );
    }
);

}
#endif

namespace
{

constexpr const char* NativeDatabasePath =
    "ibvap.db";

#ifdef __EMSCRIPTEN__
constexpr const char* WasmDatabasePath =
    "/ibvap/ibvap.db";
#endif

const char* databasePath()
{
#ifdef __EMSCRIPTEN__
    return WasmDatabasePath;
#else
    return NativeDatabasePath;
#endif
}

bool execSql(
    sqlite3* db,
    const char* sql
)
{
    char* errorMessage = nullptr;

    const int result =
        sqlite3_exec(
            db,
            sql,
            nullptr,
            nullptr,
            &errorMessage
        );

    if (result != SQLITE_OK)
    {
        std::cerr
            << "IBVAP Database: SQL error: "
            << (errorMessage != nullptr ? errorMessage : "unknown")
            << '\n';

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool bindBlob(
    sqlite3_stmt* statement,
    int index,
    const std::vector<std::uint8_t>& data
)
{
    if (data.empty())
    {
        return
            sqlite3_bind_null(
                statement,
                index
            ) == SQLITE_OK;
    }

    return
        sqlite3_bind_blob(
            statement,
            index,
            data.data(),
            static_cast<int>(
                std::min(
                    data.size(),
                    static_cast<std::size_t>(
                        std::numeric_limits<int>::max()
                    )
                )
            ),
            SQLITE_TRANSIENT
        ) == SQLITE_OK;
}

std::vector<std::uint8_t> readBlob(
    sqlite3_stmt* statement,
    int column
)
{
    const void* data =
        sqlite3_column_blob(
            statement,
            column
        );

    const int size =
        sqlite3_column_bytes(
            statement,
            column
        );

    if (data == nullptr || size <= 0)
    {
        return {};
    }

    const auto* bytes =
        static_cast<const std::uint8_t*>(
            data
        );

    return std::vector<std::uint8_t>(
        bytes,
        bytes + size
    );
}

std::string readText(
    sqlite3_stmt* statement,
    int column
)
{
    const unsigned char* value =
        sqlite3_column_text(
            statement,
            column
        );

    if (value == nullptr)
    {
        return {};
    }

    return reinterpret_cast<const char*>(value);
}


bool migrateHistoryForLicensePlates(
    sqlite3* db
)
{
    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT sql FROM sqlite_master "
        "WHERE type = 'table' AND name = 'history';";

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    std::string tableSql;

    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        tableSql =
            readText(statement, 0);
    }

    sqlite3_finalize(statement);

    /*
     * A fresh database already gets the new CHECK constraint from
     * the schema below. Only rebuild an existing history table when
     * it still has the old (1, 2) constraint.
     */
    if (
        tableSql.find("IN (1, 2)") ==
            std::string::npos &&
        tableSql.find("IN(1,2)") ==
            std::string::npos
    )
    {
        return true;
    }

    std::cout
        << "IBVAP Database: migrating history schema for "
           "license-plate records.\n";

    const char* migration = R"SQL(
BEGIN IMMEDIATE;

ALTER TABLE history RENAME TO history_legacy;

CREATE TABLE history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    detected_at_us INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    source_name TEXT NOT NULL,
    object_type INTEGER NOT NULL CHECK(object_type IN (1, 2, 3)),
    confidence REAL NOT NULL,
    image BLOB NOT NULL,
    image_width INTEGER NOT NULL,
    image_height INTEGER NOT NULL,
    image_channels INTEGER NOT NULL,
    face_image BLOB,
    face_width INTEGER NOT NULL DEFAULT 0,
    face_height INTEGER NOT NULL DEFAULT 0,
    face_channels INTEGER NOT NULL DEFAULT 0,
    identity_id INTEGER,
    FOREIGN KEY(identity_id) REFERENCES identities(id) ON DELETE SET NULL
);

INSERT INTO history (
    id,
    detected_at_us,
    source_id,
    source_name,
    object_type,
    confidence,
    image,
    image_width,
    image_height,
    image_channels,
    face_image,
    face_width,
    face_height,
    face_channels,
    identity_id
)
SELECT
    id,
    detected_at_us,
    source_id,
    source_name,
    object_type,
    confidence,
    image,
    image_width,
    image_height,
    image_channels,
    face_image,
    face_width,
    face_height,
    face_channels,
    identity_id
FROM history_legacy;

DROP TABLE history_legacy;

COMMIT;
)SQL";

    if (!execSql(db, migration))
    {
        execSql(db, "ROLLBACK;");
        return false;
    }

    return true;
}


bool migrateIdentityCategories(
    sqlite3* db
)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'identities';",
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK)
    {
        return false;
    }

    std::string tableSql;
    if (sqlite3_step(statement) == SQLITE_ROW)
        tableSql = readText(statement, 0);
    sqlite3_finalize(statement);

    const bool hasOldCategoryConstraint =
        tableSql.find("category IN (1, 2)") != std::string::npos ||
        tableSql.find("category IN(1,2)") != std::string::npos;
    if (!hasOldCategoryConstraint)
        return true;

    std::cout << "IBVAP Database: adding Special identity category.\n";

    if (!execSql(db, "PRAGMA foreign_keys = OFF;"))
        return false;

    const char* migration = R"SQL(
BEGIN IMMEDIATE;

CREATE TABLE identities_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    category INTEGER NOT NULL CHECK(category IN (1, 2, 3)),
    face_image BLOB,
    face_width INTEGER NOT NULL DEFAULT 0,
    face_height INTEGER NOT NULL DEFAULT 0,
    face_channels INTEGER NOT NULL DEFAULT 0,
    face_embedding BLOB,
    created_at_us INTEGER NOT NULL
);

INSERT INTO identities_new (
    id, name, category, face_image, face_width, face_height,
    face_channels, face_embedding, created_at_us
)
SELECT
    id, name, category, face_image, face_width, face_height,
    face_channels, face_embedding, created_at_us
FROM identities;

DROP TABLE identities;
ALTER TABLE identities_new RENAME TO identities;
COMMIT;
)SQL";

    const bool migrated = execSql(db, migration);
    if (!migrated)
        execSql(db, "ROLLBACK;");

    const bool foreignKeysRestored =
        execSql(db, "PRAGMA foreign_keys = ON;");
    return migrated && foreignKeysRestored;
}

HistoryRecord readHistoryRecord(
    sqlite3_stmt* statement
)
{
    HistoryRecord record;

    record.id =
        sqlite3_column_int64(
            statement,
            0
        );

    record.detectedAtUs =
        sqlite3_column_int64(
            statement,
            1
        );

    record.sourceId =
        readText(statement, 2);

    record.sourceName =
        readText(statement, 3);

    record.objectType =
        static_cast<DatabaseObjectType>(
            sqlite3_column_int(
                statement,
                4
            )
        );

    record.confidence =
        static_cast<float>(
            sqlite3_column_double(
                statement,
                5
            )
        );

    record.image.width =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                6
            )
        );

    record.image.height =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                7
            )
        );

    record.image.channels =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                8
            )
        );

    record.image.pixels =
        readBlob(
            statement,
            9
        );

    record.faceImage.width =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                10
            )
        );

    record.faceImage.height =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                11
            )
        );

    record.faceImage.channels =
        static_cast<std::uint32_t>(
            sqlite3_column_int(
                statement,
                12
            )
        );

    record.faceImage.pixels =
        readBlob(
            statement,
            13
        );

    if (sqlite3_column_type(statement, 14) != SQLITE_NULL)
    {
        record.identityId =
            sqlite3_column_int64(
                statement,
                14
            );
    }

    record.identityName =
        readText(statement, 15);

    if (sqlite3_column_type(statement, 16) != SQLITE_NULL)
    {
        record.identityType =
            static_cast<DatabaseIdentityType>(
                sqlite3_column_int(
                    statement,
                    16
                )
            );
    }

    return record;
}

}

struct Database::Impl
{
    sqlite3* db = nullptr;
    bool open = false;
    bool ready = false;
};

Database::Database()
    : m_impl(new Impl())
{
}

Database::~Database()
{
    close();
    delete m_impl;
    m_impl = nullptr;
}

bool Database::open()
{
    if (m_impl == nullptr)
    {
        return false;
    }

    if (m_impl->open)
    {
        return true;
    }

#ifdef __EMSCRIPTEN__
    ibvap_database_prepare_storage();

    while (
        !ibvap_database_storage_ready() &&
        !ibvap_database_storage_error()
    )
    {
        emscripten_sleep(5);
    }

    if (ibvap_database_storage_error())
    {
        std::cerr
            << "IBVAP Database: browser storage initialization failed.\n";
        return false;
    }
#endif

    const int flags =
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX;

    const int result =
        sqlite3_open_v2(
            databasePath(),
            &m_impl->db,
            flags,
            nullptr
        );

    if (result != SQLITE_OK || m_impl->db == nullptr)
    {
        std::cerr
            << "IBVAP Database: failed to open database: "
            << (
                m_impl->db != nullptr
                    ? sqlite3_errmsg(m_impl->db)
                    : "unknown"
            )
            << '\n';

        if (m_impl->db != nullptr)
        {
            sqlite3_close(m_impl->db);
            m_impl->db = nullptr;
        }

        return false;
    }

    if (!execSql(m_impl->db, "PRAGMA foreign_keys = ON;"))
    {
        close();
        return false;
    }

#ifdef __EMSCRIPTEN__
    if (!execSql(m_impl->db, "PRAGMA journal_mode = DELETE;"))
#else
    if (!execSql(m_impl->db, "PRAGMA journal_mode = WAL;"))
#endif
    {
        close();
        return false;
    }

    if (!migrateHistoryForLicensePlates(m_impl->db))
    {
        close();
        return false;
    }

    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);

INSERT INTO schema_version(version)
SELECT 1
WHERE NOT EXISTS (SELECT 1 FROM schema_version);

CREATE TABLE IF NOT EXISTS identities (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    category INTEGER NOT NULL CHECK(category IN (1, 2, 3)),
    face_image BLOB,
    face_width INTEGER NOT NULL DEFAULT 0,
    face_height INTEGER NOT NULL DEFAULT 0,
    face_channels INTEGER NOT NULL DEFAULT 0,
    face_embedding BLOB,
    created_at_us INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    detected_at_us INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    source_name TEXT NOT NULL,
    object_type INTEGER NOT NULL CHECK(object_type IN (1, 2, 3)),
    confidence REAL NOT NULL,
    image BLOB NOT NULL,
    image_width INTEGER NOT NULL,
    image_height INTEGER NOT NULL,
    image_channels INTEGER NOT NULL,
    face_image BLOB,
    face_width INTEGER NOT NULL DEFAULT 0,
    face_height INTEGER NOT NULL DEFAULT 0,
    face_channels INTEGER NOT NULL DEFAULT 0,
    identity_id INTEGER,
    FOREIGN KEY(identity_id) REFERENCES identities(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_history_detected_at
ON history(detected_at_us DESC);

CREATE INDEX IF NOT EXISTS idx_history_identity
ON history(identity_id);
)SQL";

    if (!execSql(m_impl->db, schema))
    {
        close();
        return false;
    }

    if (!migrateIdentityCategories(m_impl->db))
    {
        close();
        return false;
    }

    m_impl->open = true;
    m_impl->ready = true;

    std::cout
        << "IBVAP Database: SQLite database ready.\n";

    return true;
}

void Database::close() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }

    if (m_impl->db != nullptr)
    {
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
    }

#ifdef __EMSCRIPTEN__
    if (m_impl->open)
    {
        ibvap_database_sync();
    }
#endif

    m_impl->open = false;
    m_impl->ready = false;
}

bool Database::isOpen() const noexcept
{
    return m_impl != nullptr && m_impl->open;
}

bool Database::isReady() const noexcept
{
    return m_impl != nullptr && m_impl->ready;
}

bool Database::insertHistory(
    const HistoryRecord& record,
    std::int64_t& historyId
)
{
    historyId = 0;

    if (!isReady())
    {
        return false;
    }

    const char* sql = R"SQL(
INSERT INTO history (
    detected_at_us,
    source_id,
    source_name,
    object_type,
    confidence,
    image,
    image_width,
    image_height,
    image_channels,
    face_image,
    face_width,
    face_height,
    face_channels
)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
)SQL";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            m_impl->db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    const bool bound =
        sqlite3_bind_int64(
            statement,
            1,
            record.detectedAtUs
        ) == SQLITE_OK &&
        sqlite3_bind_text(
            statement,
            2,
            record.sourceId.c_str(),
            -1,
            SQLITE_TRANSIENT
        ) == SQLITE_OK &&
        sqlite3_bind_text(
            statement,
            3,
            record.sourceName.c_str(),
            -1,
            SQLITE_TRANSIENT
        ) == SQLITE_OK &&
        sqlite3_bind_int(
            statement,
            4,
            static_cast<int>(record.objectType)
        ) == SQLITE_OK &&
        sqlite3_bind_double(
            statement,
            5,
            record.confidence
        ) == SQLITE_OK &&
        bindBlob(statement, 6, record.image.pixels) &&
        sqlite3_bind_int(statement, 7, static_cast<int>(record.image.width)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 8, static_cast<int>(record.image.height)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 9, static_cast<int>(record.image.channels)) == SQLITE_OK &&
        bindBlob(statement, 10, record.faceImage.pixels) &&
        sqlite3_bind_int(statement, 11, static_cast<int>(record.faceImage.width)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 12, static_cast<int>(record.faceImage.height)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 13, static_cast<int>(record.faceImage.channels)) == SQLITE_OK;

    const bool success =
        bound &&
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    if (!success)
    {
        return false;
    }

    historyId =
        sqlite3_last_insert_rowid(
            m_impl->db
        );

#ifdef __EMSCRIPTEN__
    ibvap_database_sync();
#endif

    return historyId > 0;
}

bool Database::listHistory(
    std::vector<HistoryRecord>& records,
    std::size_t limit
)
{
    records.clear();

    if (!isReady())
    {
        return false;
    }

    limit =
        std::min<std::size_t>(
            limit,
            1000
        );

    const char* sql = R"SQL(
SELECT
    h.id,
    h.detected_at_us,
    h.source_id,
    h.source_name,
    h.object_type,
    h.confidence,
    h.image_width,
    h.image_height,
    h.image_channels,
    h.image,
    h.face_width,
    h.face_height,
    h.face_channels,
    h.face_image,
    h.identity_id,
    COALESCE(i.name, ''),
    COALESCE(i.category, 1)
FROM history h
LEFT JOIN identities i
    ON i.id = h.identity_id
ORDER BY h.detected_at_us DESC
LIMIT ?;
)SQL";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            m_impl->db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    sqlite3_bind_int64(
        statement,
        1,
        static_cast<sqlite3_int64>(limit)
    );

    while (
        sqlite3_step(statement) == SQLITE_ROW
    )
    {
        records.push_back(
            readHistoryRecord(statement)
        );
    }

    sqlite3_finalize(statement);
    return true;
}

bool Database::createIdentity(
    DatabaseIdentityType type,
    const std::string& name,
    const DatabaseImage& faceImage,
    const std::vector<std::uint8_t>& faceEmbedding,
    std::int64_t& identityId
)
{
    identityId = 0;

    if (!isReady() || name.empty())
    {
        return false;
    }

    const char* sql = R"SQL(
INSERT INTO identities (
    name,
    category,
    face_image,
    face_width,
    face_height,
    face_channels,
    face_embedding,
    created_at_us
)
VALUES (?, ?, ?, ?, ?, ?, ?, ?);
)SQL";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            m_impl->db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    const bool bound =
        sqlite3_bind_text(
            statement,
            1,
            name.c_str(),
            -1,
            SQLITE_TRANSIENT
        ) == SQLITE_OK &&
        sqlite3_bind_int(
            statement,
            2,
            static_cast<int>(type)
        ) == SQLITE_OK &&
        bindBlob(statement, 3, faceImage.pixels) &&
        sqlite3_bind_int(statement, 4, static_cast<int>(faceImage.width)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 5, static_cast<int>(faceImage.height)) == SQLITE_OK &&
        sqlite3_bind_int(statement, 6, static_cast<int>(faceImage.channels)) == SQLITE_OK &&
        bindBlob(statement, 7, faceEmbedding) &&
        sqlite3_bind_int64(
            statement,
            8,
            static_cast<sqlite3_int64>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            )
        ) == SQLITE_OK;

    const bool success =
        bound &&
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    if (!success)
    {
        return false;
    }

    identityId =
        sqlite3_last_insert_rowid(
            m_impl->db
        );

#ifdef __EMSCRIPTEN__
    ibvap_database_sync();
#endif

    return identityId > 0;
}

bool Database::assignHistoryIdentity(
    std::int64_t historyId,
    std::int64_t identityId
)
{
    if (!isReady() || historyId <= 0 || identityId <= 0)
    {
        return false;
    }

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            m_impl->db,
            "UPDATE history SET identity_id = ? WHERE id = ?;",
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    const bool success =
        sqlite3_bind_int64(statement, 1, identityId) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 2, historyId) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

#ifdef __EMSCRIPTEN__
    if (success)
    {
        ibvap_database_sync();
    }
#endif

    return success;
}

bool Database::listIdentities(
    DatabaseIdentityType type,
    std::vector<HistoryRecord>& records
)
{
    records.clear();

    if (!isReady())
    {
        return false;
    }

    const char* sql = R"SQL(
SELECT
    id,
    created_at_us,
    '',
    name,
    1,
    1.0,
    face_width,
    face_height,
    face_channels,
    face_image,
    face_width,
    face_height,
    face_channels,
    face_image,
    id,
    name,
    category
FROM identities
WHERE category = ?
ORDER BY created_at_us DESC;
)SQL";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            m_impl->db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    )
    {
        return false;
    }

    sqlite3_bind_int(
        statement,
        1,
        static_cast<int>(type)
    );

    while (
        sqlite3_step(statement) == SQLITE_ROW
    )
    {
        records.push_back(
            readHistoryRecord(statement)
        );
    }

    sqlite3_finalize(statement);
    return true;
}
