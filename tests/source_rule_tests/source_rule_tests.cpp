#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxCodeLines = 800;

[[nodiscard]] bool isCodeFile(const std::filesystem::path& path)
{
    const auto extension = path.extension().string();
    return extension == ".c"
        || extension == ".cpp"
        || extension == ".h"
        || extension == ".hpp";
}

[[nodiscard]] bool isCodeRoot(const std::filesystem::path& path)
{
    const auto name = path.filename().string();
    return name == "engine"
        || name == "editor"
        || name == "server"
        || name == "client"
        || name == "tools"
        || name == "tests";
}

[[nodiscard]] std::size_t lineCount(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::size_t count = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++count;
    }
    return count;
}

} // namespace

int main()
{
    const auto sourceRoot = std::filesystem::path(PROJECTUNITY_SOURCE_DIR);
    std::vector<std::filesystem::path> violations;
    for (const auto& root : std::filesystem::directory_iterator(sourceRoot)) {
        if (!root.is_directory() || !isCodeRoot(root.path())) {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
            if (!entry.is_regular_file() || !isCodeFile(entry.path())) {
                continue;
            }

            if (lineCount(entry.path()) > kMaxCodeLines) {
                violations.push_back(std::filesystem::relative(entry.path(), sourceRoot));
            }
        }
    }

    if (!violations.empty()) {
        std::cerr << "Code files above " << kMaxCodeLines << " lines:\n";
        for (const auto& path : violations) {
            std::cerr << "  " << path.string() << '\n';
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
