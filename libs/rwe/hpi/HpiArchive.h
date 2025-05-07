#pragma once

#include <istream>
#include <memory>
#include <vector>

namespace rwe
{
    class HpiArchive
    {
    public:
        struct DirectoryEntry;
        struct File
        {
            enum class CompressionScheme
            {
                None = 0,
                LZ77,
                ZLib
            };
            CompressionScheme compressionScheme;
            std::size_t offset;
            std::size_t size;
        };
        struct Directory
        {
            std::vector<DirectoryEntry> entries;
        };
        struct DirectoryEntry
        {
            std::string name;
            std::shared_ptr<File> file;
            std::shared_ptr<Directory> directory;

            DirectoryEntry(const std::string &name, std::shared_ptr<Directory> directory) :
                name(name),
                directory(directory)
            { }

            DirectoryEntry(const std::string &name, std::shared_ptr<File> file) :
                name(name),
                file(file)
            { }

        };

    private:
        std::istream* stream;
        unsigned char decryptionKey;
        Directory _root;

    public:
        explicit HpiArchive(std::istream* stream);

        const Directory& root() const;
        const File* findFile(const std::string& path) const;
        const Directory* findDirectory(const std::string& path) const;
        void extract(const File& file, char* buffer) const;

        std::vector<std::string> findRootDirectoriesWithPrefix(const std::string& prefix) const;
    };

}
