#include "Updater.h"

#ifndef PATTERNSPACE_UPDATE_REPO
 #define PATTERNSPACE_UPDATE_REPO "thomas-nelson23/Patternspace"
#endif
#ifndef PATTERNSPACE_UPDATE_URL
 #define PATTERNSPACE_UPDATE_URL "https://api.github.com/repos/" PATTERNSPACE_UPDATE_REPO "/releases/latest"
#endif

namespace
{
constexpr auto checkAtLaunchKey = "checkAtLaunch", lastCheckKey = "lastCheck", skipVersionKey = "skipVersion";
constexpr juce::int64 checkIntervalMs = 24 * 60 * 60 * 1000;

juce::String userAgent() { return "User-Agent: Patternspace/" + Updater::currentVersion(); }

juce::String installHint()
{
#if JUCE_MAC
    return "Quit Patternspace, unzip the download and drag Standalone/Patternspace.app into Applications, replacing the old one.";
#elif JUCE_WINDOWS
    return "Quit Patternspace, unzip the download and replace your Patternspace.exe with the one in its Standalone folder.";
#else
    return "Quit Patternspace, unzip the download and replace your Patternspace binary with the one in its Standalone folder.";
#endif
}

juce::File downloadsFolder()
{
    const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    return downloads.isDirectory() ? downloads : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}
} // namespace

Updater::Updater (juce::Component& p) : parent (&p)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Patternspace Updates";
    o.filenameSuffix = "settings";
    o.folderName = "Patternspace";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (o);
    self = this;
}

Updater::~Updater()
{
    if (task != nullptr)
    {
        task.reset();
        target.deleteFile();
    }
    progressWindow.reset();
}

juce::String Updater::currentVersion() { return JucePlugin_VersionString; }

bool Updater::checksAtLaunch() const { return settings->getBoolValue (checkAtLaunchKey, true); }

void Updater::setChecksAtLaunch (bool on)
{
    settings->setValue (checkAtLaunchKey, on);
    settings->saveIfNeeded();
}

void Updater::checkNow() { startCheck (true); }

void Updater::checkIfDue()
{
    const auto last = (juce::int64) settings->getValue (lastCheckKey, "0").getLargeIntValue();
    if (checksAtLaunch() && juce::Time::currentTimeMillis() - last >= checkIntervalMs)
        startCheck (false);
}

void Updater::startCheck (bool user)
{
    if (checking)
        return;
    checking = true;

    // A detached thread, so quitting never waits on a slow connection; the result only comes back
    // if this updater still exists.
    juce::Thread::launch ([weak = self, user] {
        int status = 0;
        const juce::URL url (PATTERNSPACE_UPDATE_URL);
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\n" + userAgent())
                                                 .withConnectionTimeoutMs (10000)
                                                 .withStatusCode (&status));
        auto result = Result::failed;
        ps::update::Release release;
        if (status == 404)
            result = Result::noRelease;
        else if (stream != nullptr && status == 200
                 && ps::update::parseRelease (juce::JSON::parse (stream->readEntireStreamAsString()), ps::update::platformName(), release))
            result = ps::update::compareVersions (release.version, currentVersion()) > 0 ? Result::found : Result::upToDate;

        juce::MessageManager::callAsync ([weak, result, release, user] {
            if (auto* u = weak.get())
                u->checkFinished (result, release, user);
        });
    });
}

void Updater::checkFinished (Result result, const ps::update::Release& release, bool user)
{
    checking = false;
    if (result != Result::failed)
    {
        settings->setValue (lastCheckKey, juce::String (juce::Time::currentTimeMillis()));
        settings->saveIfNeeded();
    }

    const bool nowAvailable = result == Result::found;
    if (nowAvailable != available)
    {
        available = nowAvailable;
        if (onStateChanged)
            onStateChanged();
    }

    auto tell = [this] (juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message) {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (icon, title, message).withAssociatedComponent (parent), nullptr);
    };

    switch (result)
    {
        case Result::found:
            if (user || release.version != settings->getValue (skipVersionKey))
                offer (release);
            break;
        case Result::upToDate:
            if (user)
                tell (juce::MessageBoxIconType::InfoIcon, "You're up to date", "Patternspace " + currentVersion() + " is the latest version.");
            break;
        case Result::noRelease:
            if (user)
                tell (juce::MessageBoxIconType::InfoIcon, "No updates yet", "No Patternspace releases have been published yet.");
            break;
        case Result::failed:
            if (user)
                tell (juce::MessageBoxIconType::WarningIcon, "Couldn't check for updates",
                      "Patternspace couldn't reach GitHub. Check your internet connection and try again.");
            break;
    }
}

void Updater::offer (const ps::update::Release& release)
{
    auto message = "You have " + currentVersion() + ".";
    if (release.notes.isNotEmpty())
        message << "\n\n" << (release.notes.length() > 800 ? release.notes.substring (0, 800).trimEnd() + "..." : release.notes);
    if (release.assetUrl.isEmpty())
        message << "\n\nThis release has no download for " << ps::update::platformName() << ".";

    const auto action = release.assetUrl.isNotEmpty() ? "Download" : "Open release page";
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::InfoIcon)
                             .withTitle ("Patternspace " + release.version + " is available")
                             .withMessage (message)
                             .withButton (action)
                             .withButton ("Skip this version")
                             .withButton ("Later")
                             .withAssociatedComponent (parent);

    juce::AlertWindow::showAsync (options, [weak = self, release] (int choice) {
        auto* u = weak.get();
        if (u == nullptr)
            return;
        if (choice == 1)
        {
            if (release.assetUrl.isNotEmpty())
                u->download (release);
            else
                juce::URL (release.pageUrl).launchInDefaultBrowser();
        }
        else if (choice == 2)
        {
            u->settings->setValue (skipVersionKey, release.version);
            u->settings->saveIfNeeded();
        }
    });
}

void Updater::download (const ps::update::Release& release)
{
    if (task != nullptr)
        return;
    pending = release;
    target = downloadsFolder().getChildFile (release.assetName).getNonexistentSibling (false);
    progressValue = -1.0;

    task = juce::URL (release.assetUrl).downloadToFile (target, juce::URL::DownloadTaskOptions().withExtraHeaders (userAgent()).withListener (this));
    if (task == nullptr)
    {
        showDownloadFailed();
        return;
    }

    progressWindow = std::make_unique<juce::AlertWindow> ("Downloading Patternspace " + release.version,
                                                          release.assetName + " to " + target.getParentDirectory().getFullPathName(),
                                                          juce::MessageBoxIconType::NoIcon, parent);
    progressWindow->addProgressBarComponent (progressValue);
    progressWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    progressWindow->enterModalState (true, juce::ModalCallbackFunction::create ([weak = self] (int) {
                                         if (auto* u = weak.get())
                                             u->cancelDownload();
                                     }),
                                     false);
}

void Updater::progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total)
{
    const auto fallback = pending.assetSize;
    juce::MessageManager::callAsync ([weak = self, done, total, fallback] {
        if (auto* u = weak.get())
        {
            const auto size = total > 0 ? total : fallback;
            u->progressValue = size > 0 ? juce::jlimit (0.0, 1.0, (double) done / (double) size) : -1.0;
        }
    });
}

void Updater::finished (juce::URL::DownloadTask* t, bool success)
{
    const bool ok = success && ! t->hadError() && (t->statusCode() == 0 || t->statusCode() < 400);
    juce::MessageManager::callAsync ([weak = self, ok] {
        if (auto* u = weak.get())
            u->downloadFinished (ok);
    });
}

void Updater::downloadFinished (bool success)
{
    if (task == nullptr) // cancelled
        return;
    // Drop the task before closing the window, so the window's callback doesn't treat this as a cancel.
    task.reset();
    progressWindow.reset();

    if (! success || ! target.existsAsFile())
    {
        target.deleteFile();
        showDownloadFailed();
        return;
    }

    if (pending.isInstaller)
    {
        // Patternspace quits as the installer opens, so the installer can replace the app.
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("Patternspace " + pending.version + " downloaded")
                                          .withMessage ("The installer is in " + target.getFullPathName()
                                                        + ". It updates the app and the plug-ins.\n\n"
                                                          "Install now quits Patternspace and opens it.")
                                          .withButton ("Install now")
                                          .withButton ("Show in folder")
                                          .withButton ("Later")
                                          .withAssociatedComponent (parent),
                                      [file = target] (int choice) {
                                          if (choice == 1 && file.startAsProcess())
                                          {
                                              if (auto* app = juce::JUCEApplicationBase::getInstance())
                                                  app->systemRequestedQuit();
                                          }
                                          else if (choice == 1 || choice == 2)
                                              file.revealToUser();
                                      });
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("Patternspace " + pending.version + " downloaded")
                                      .withMessage ("It's in " + target.getFullPathName() + ". The zip has the standalone app and the plug-ins.\n\n"
                                                    + installHint())
                                      .withButton ("Show in folder")
                                      .withButton ("OK")
                                      .withAssociatedComponent (parent),
                                  [file = target] (int choice) {
                                      if (choice == 1)
                                          file.revealToUser();
                                  });
}

void Updater::showDownloadFailed()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("The download didn't finish")
                                      .withMessage ("Patternspace couldn't download " + pending.assetName
                                                    + ". You can get it from the release page instead.")
                                      .withButton ("Open release page")
                                      .withButton ("Close")
                                      .withAssociatedComponent (parent),
                                  [url = pending.pageUrl] (int choice) {
                                      if (choice == 1)
                                          juce::URL (url).launchInDefaultBrowser();
                                  });
}

void Updater::cancelDownload()
{
    if (task == nullptr)
        return;
    task.reset();
    target.deleteFile();
    juce::MessageManager::callAsync ([weak = self] {
        if (auto* u = weak.get())
            u->progressWindow.reset();
    });
}
