# Changelog

All notable changes to this project are documented here. The format follows Keep a Changelog; versions follow semantic versioning.

## [1.1.0] - 2026-10-10

### Added
- Mutes the Spotify desktop app while an ad plays and restores your previous volume as soon as the music is back. Spotify's own promotions for its premium plan are muted too.
- Optional mode that only lowers Spotify to a percentage you choose instead of muting it.
- Optional setting for videos: lower, mute or pause Spotify while a browser or video player in another app plays sound for a few seconds. Pausing resumes the music when the video ends. Lowering has its own level, independent of the ad level. Off by default; short sounds, games and calls are ignored.
- Tray menu with a status line (for example "Ad: muted"), an on/off switch and the settings.
- Start with Windows, optionally without a tray icon. Opening AdMutify again brings the icon back. The setting follows the switch in Task Manager's startup apps.
- Ten languages: English, German, Spanish, Brazilian Portuguese, Italian, Dutch, Polish, Turkish, Japanese and Simplified Chinese, in the app and in the installer.
- Settings window and tray menu in the Windows 11 style with rounded corners, light and dark mode.
- Installer for the current user or all users, with an option to skip the Start menu folder. Installing a newer version simply updates the existing one.
- Portable version as a single `AdMutify.exe` that keeps its settings next to the program.

### Fixed
- Spotify is unmuted again even when it was closed, or AdMutify was ended or crashed, in the middle of an ad.
- Spotify is also found when it starts hidden in the tray after AdMutify.
- If the volume could not be restored after a crash, the crash notice says so and points to the Windows volume mixer.
- Old log files and crash reports (older than 7 days) are cleaned up automatically.
