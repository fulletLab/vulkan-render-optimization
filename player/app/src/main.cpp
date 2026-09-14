#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/core/Log.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/VulkanRenderer.hpp>
#include <projectunity/scene/Scene.hpp>
#include <projectunity/scripting/ScriptRuntime.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QTextStream>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

#if defined(_WIN32)
constexpr const char* kProjectScriptsModuleFileName = "ProjectUnityGameScripts.dll";
#elif defined(__APPLE__)
constexpr const char* kProjectScriptsModuleFileName = "libProjectUnityGameScripts.dylib";
#else
constexpr const char* kProjectScriptsModuleFileName = "libProjectUnityGameScripts.so";
#endif

struct PlayerConfig {
    QString title {"ProjectUnity Player"};
    std::filesystem::path manifestPath;
    std::filesystem::path scenePath;
    std::filesystem::path assetCachePath;
    std::filesystem::path scriptModulePath;
    bool requireScripts {false};
    bool smokeTest {false};
    int windowWidth {1280};
    int windowHeight {720};
};

[[nodiscard]] std::filesystem::path pathFromQString(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] std::filesystem::path resolvePath(const std::filesystem::path& base, const QString& text)
{
    auto path = pathFromQString(text);
    if (path.is_absolute()) {
        return path;
    }
    return base / path;
}

[[nodiscard]] std::filesystem::path appDir()
{
    return pathFromQString(QCoreApplication::applicationDirPath());
}

[[nodiscard]] bool readManifest(PlayerConfig& config, QString* errorMessage)
{
    QFile file(pathToQString(config.manifestPath));
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No se pudo abrir el manifiesto: %1").arg(pathToQString(config.manifestPath));
        }
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Manifiesto invalido: %1").arg(parseError.errorString());
        }
        return false;
    }

    const auto root = document.object();
    const auto base = config.manifestPath.parent_path();
    if (root.contains(QStringLiteral("name"))) {
        config.title = root.value(QStringLiteral("name")).toString(config.title);
    }
    if (root.contains(QStringLiteral("scene"))) {
        config.scenePath = resolvePath(base, root.value(QStringLiteral("scene")).toString());
    }
    if (root.contains(QStringLiteral("assetCache"))) {
        config.assetCachePath = resolvePath(base, root.value(QStringLiteral("assetCache")).toString());
    }
    if (root.contains(QStringLiteral("scripts"))) {
        config.scriptModulePath = resolvePath(base, root.value(QStringLiteral("scripts")).toString());
        config.requireScripts = root.value(QStringLiteral("requireScripts")).toBool(true);
    }
    config.windowWidth = root.value(QStringLiteral("windowWidth")).toInt(config.windowWidth);
    config.windowHeight = root.value(QStringLiteral("windowHeight")).toInt(config.windowHeight);
    return true;
}

[[nodiscard]] bool parseCommandLine(PlayerConfig& config, QString* errorMessage)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ProjectUnity standalone game player"));
    parser.addHelpOption();
    const QCommandLineOption projectOption(
        {QStringLiteral("project"), QStringLiteral("manifest")},
        QStringLiteral("Path to game.projectunity.json."),
        QStringLiteral("path"));
    const QCommandLineOption sceneOption(
        QStringLiteral("scene"),
        QStringLiteral("Scene file to load."),
        QStringLiteral("path"));
    const QCommandLineOption assetCacheOption(
        QStringLiteral("asset-cache"),
        QStringLiteral("Cooked asset cache directory."),
        QStringLiteral("path"));
    const QCommandLineOption scriptsOption(
        QStringLiteral("scripts"),
        QStringLiteral("Native script module to load."),
        QStringLiteral("path"));
    const QCommandLineOption titleOption(
        QStringLiteral("title"),
        QStringLiteral("Window title."),
        QStringLiteral("text"));
    const QCommandLineOption requireScriptsOption(
        QStringLiteral("require-scripts"),
        QStringLiteral("Fail startup if the script module cannot be loaded."));
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Load the project, create the runtime window once, then exit."));

    parser.addOptions({
        projectOption,
        sceneOption,
        assetCacheOption,
        scriptsOption,
        titleOption,
        requireScriptsOption,
        smokeTestOption,
    });
    if (!parser.parse(QCoreApplication::arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return false;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }

    const auto base = appDir();
    config.manifestPath = parser.isSet(projectOption)
        ? pathFromQString(parser.value(projectOption))
        : base / "game.projectunity.json";
    config.smokeTest = parser.isSet(smokeTestOption);
    if (parser.isSet(titleOption)) {
        config.title = parser.value(titleOption);
    }

    const bool manifestExists = std::filesystem::exists(config.manifestPath);
    if (manifestExists && !readManifest(config, errorMessage)) {
        return false;
    }
    if (!manifestExists && !parser.isSet(sceneOption)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "No encontre game.projectunity.json junto al ejecutable.\n\n"
                "Ruta esperada:\n%1\n\n"
                "Abre el .exe dentro de la carpeta exportada del juego, o pasa --scene.")
                .arg(pathToQString(config.manifestPath));
        }
        return false;
    }

    if (parser.isSet(sceneOption)) {
        config.scenePath = pathFromQString(parser.value(sceneOption));
    }
    if (parser.isSet(assetCacheOption)) {
        config.assetCachePath = pathFromQString(parser.value(assetCacheOption));
    }
    if (parser.isSet(scriptsOption)) {
        config.scriptModulePath = pathFromQString(parser.value(scriptsOption));
    }
    if (parser.isSet(requireScriptsOption)) {
        config.requireScripts = true;
    }
    if (config.assetCachePath.empty()) {
        config.assetCachePath = base / "Content" / "Assets";
    }
    if (config.scriptModulePath.empty()) {
        config.scriptModulePath = base / "Scripts" / kProjectScriptsModuleFileName;
    }
    if (config.scenePath.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "Falta la escena. Incluye \"scene\" en game.projectunity.json o usa --scene.\n\n"
                "Manifiesto usado:\n%1")
                .arg(pathToQString(config.manifestPath));
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool validateConfig(const PlayerConfig& config, QString* errorMessage)
{
    if (!std::filesystem::exists(config.scenePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("La escena no existe: %1").arg(pathToQString(config.scenePath));
        }
        return false;
    }
    if (!std::filesystem::exists(config.assetCachePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("El cache de assets no existe: %1").arg(pathToQString(config.assetCachePath));
        }
        return false;
    }
    if (config.requireScripts && !std::filesystem::exists(config.scriptModulePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("El modulo de scripts no existe: %1").arg(pathToQString(config.scriptModulePath));
        }
        return false;
    }
    return true;
}

void printError(const QString& message)
{
    std::cerr << message.toStdString() << '\n';
    projectunity::core::logError(projectunity::core::LogCategory::Core, message.toStdString());
}

void showStartupError(const QString& message)
{
    printError(message);
    QMessageBox::critical(nullptr, QStringLiteral("ProjectUnity Player"), message);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ProjectUnity Player"));
    QApplication::setOrganizationName(QStringLiteral("ProjectUnity"));
    QApplication::setOrganizationDomain(QStringLiteral("projectunity.local"));

    PlayerConfig config;
    QString error;
    if (!parseCommandLine(config, &error) || !validateConfig(config, &error)) {
        showStartupError(error);
        return 2;
    }

    projectunity::scene::Scene scene;
    std::string sceneError;
    if (!scene.loadFromFile(config.scenePath, &sceneError)) {
        showStartupError(QStringLiteral("No se pudo cargar la escena: %1").arg(QString::fromStdString(sceneError)));
        return 2;
    }

    projectunity::assets::AssetManager assetManager(config.assetCachePath);
    projectunity::scripting::ScriptRegistry scriptRegistry;
    projectunity::scripting::ScriptModuleLoader scriptModuleLoader;
    projectunity::scripting::ScriptRuntime scriptRuntime;

    std::string scriptError;
    if (std::filesystem::exists(config.scriptModulePath)) {
        if (!scriptModuleLoader.load(config.scriptModulePath, scriptRegistry, &scriptError)) {
            showStartupError(QStringLiteral("No se pudo cargar scripts: %1").arg(QString::fromStdString(scriptError)));
            return 2;
        }
    } else {
        projectunity::scripting::registerBuiltInScripts(scriptRegistry);
        if (config.requireScripts) {
            showStartupError(QStringLiteral("El modulo de scripts es obligatorio y no existe: %1").arg(pathToQString(config.scriptModulePath)));
            return 2;
        }
    }
    scriptRuntime.setRegistry(&scriptRegistry);

    const auto cameraEntityId = projectunity::scripting::findRuntimeCameraEntity(scene);
    if (!cameraEntityId.isValid()) {
        showStartupError(QStringLiteral("La escena no tiene una camara runtime valida."));
        return 2;
    }

    projectunity::renderer::RendererConfig rendererConfig;
    rendererConfig.applicationName = config.title.toStdString();
    std::string rendererError;
    auto renderer = projectunity::renderer::createVulkanRenderer(rendererConfig, &rendererError);
    if (renderer == nullptr || !renderer->isReady()) {
        showStartupError(QStringLiteral("Renderer Vulkan no disponible: %1").arg(QString::fromStdString(rendererError)));
        return 2;
    }

    QMainWindow window;
    window.setWindowTitle(config.title);
    auto* viewport = new projectunity::editor::ViewportWidget(projectunity::editor::ViewportMode::Game, &window);
    viewport->setAssetManager(&assetManager);
    viewport->setRenderer(renderer.get());
    viewport->setScene(&scene);
    viewport->setGameCameraEntity(cameraEntityId);
    viewport->setGameScriptRuntime(&scriptRuntime);
    viewport->setGameRuntimeSnapshotEnabled(true);
    viewport->setGameInputEnabled(true);
    window.setCentralWidget(viewport);
    window.resize(config.windowWidth, config.windowHeight);

    if (!scriptRuntime.start(scene)) {
        showStartupError(QStringLiteral("No se pudo iniciar el runtime de scripts."));
        viewport->setRenderer(nullptr);
        return 2;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        viewport->setGameInputEnabled(false);
        viewport->setGameScriptRuntime(nullptr);
        viewport->setRenderer(nullptr);
        scriptRuntime.stop();
        scriptModuleLoader.unload(&scriptRegistry);
    });

    window.show();
    window.raise();
    window.activateWindow();

    if (config.smokeTest) {
        QApplication::processEvents();
        viewport->update();
        QApplication::processEvents();
        viewport->setGameInputEnabled(false);
        viewport->setGameScriptRuntime(nullptr);
        viewport->setRenderer(nullptr);
        scriptRuntime.stop();
        scriptModuleLoader.unload(&scriptRegistry);
        return 0;
    }

    return QApplication::exec();
}
