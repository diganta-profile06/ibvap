#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class DatabaseIdentityType : std::int32_t
{
    Ally = 1,
    Criminal = 2,
    Special = 3
};

enum class DatabaseObjectType : std::int32_t
{
    Person = 1,
    Vehicle = 2,
    LicensePlate = 3
};

struct DatabaseImage
{
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
};

struct HistoryRecord
{
    std::int64_t id = 0;
    std::int64_t detectedAtUs = 0;

    std::string sourceId;
    std::string sourceName;

    DatabaseObjectType objectType = DatabaseObjectType::Person;
    float confidence = 0.0f;

    DatabaseImage image;
    DatabaseImage faceImage;

    std::int64_t identityId = 0;
    std::string identityName;
    DatabaseIdentityType identityType = DatabaseIdentityType::Ally;
};

class Database final
{
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open();
    void close() noexcept;

    bool isOpen() const noexcept;
    bool isReady() const noexcept;

    bool insertHistory(
        const HistoryRecord& record,
        std::int64_t& historyId
    );

    bool listHistory(
        std::vector<HistoryRecord>& records,
        std::size_t limit = 200
    );

    bool createIdentity(
        DatabaseIdentityType type,
        const std::string& name,
        const DatabaseImage& faceImage,
        const std::vector<std::uint8_t>& faceEmbedding,
        std::int64_t& identityId
    );

    bool assignHistoryIdentity(
        std::int64_t historyId,
        std::int64_t identityId
    );

    bool listIdentities(
        DatabaseIdentityType type,
        std::vector<HistoryRecord>& records
    );

private:
    struct Impl;
    Impl* m_impl = nullptr;
};
