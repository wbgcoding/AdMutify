# Changelog

All notable changes to this project are documented here. The format follows Keep a Changelog; versions follow semantic versioning.

## [1.2.2] - unreleased

### Changed
- New icon: a white speaker on a violet tile. While an ad is muted the tray icon turns coral with an X beside the speaker, while AdMutify is off it turns grey. The tray icon fills its space for a larger symbol.
- The reset button asks "Reset settings?" while it waits for the second click.
- The ad counter in the settings window rolls up like an odometer when an ad is muted, and large numbers get thousands separators in the window's language (1.000 in German).
- The tray menu shows the ad counter on the status line, shortened to 12K or 1,2M once it gets large.
- "During videos in other apps" is now just "During videos"; its tooltip names the other apps. The settings window can be narrower.
- The version line at the bottom of the settings window has no separator dot.
- The icon in the title bar is sharp at every scaling, and the sound wave sits a little further from the speaker.

### Fixed
- An ad titled only "Spotify" or "Spotify Free" that starts after a long pause is muted reliably, instead of sometimes being missed when its first moment was still silent.
- When Spotify was closed abruptly and then started straight into the system tray, ads are detected again without a restart of AdMutify.
- When the settings file was locked at start, a change made during that time is no longer undone by the next change once the file can be read again. This now also holds for a value set back to its default and for "Reset settings", and the open settings window shows the values in use afterwards.
- After AdMutify was closed before it could show a crash report, the report no longer holds Spotify muted at the next start: the volume is restored first, and the report says whether that worked.
- Closing AdMutify (or updating it) while the music file or folder picker is open closes the picker first instead of risking a crash.
- Choosing a very large folder without music no longer freezes AdMutify while it looks for a music file.
- The music picker opens in the folder of a chosen file that has since been deleted, instead of in the Music folder.
- If the crash helper could not start, a crash still restores Spotify's volume right away instead of only at the next start.
- Moving the settings window to a monitor with different scaling resizes it once to the right size, instead of a broken layout with an unreachable Close button.
- An ad is counted only once its mute reaches Spotify, and the counter file can no longer be changed with a text editor.
- A portable copy installed silently while AdMutify was running starts with its own settings next to the exe instead of the installed copy's settings.

## [1.2.1] - 2026-11-14

### Changed
- New icon: a grey speaker with a green X. While an ad is muted the tray icon turns green, while AdMutify is off it turns grey.
- Buttons, the on/off switch, check boxes, radio buttons and sliders follow the Windows accent colour, with white text on dark mode instead of black on light blue.
- Log files and crash reports are now kept for 30 days instead of 7, so older problems can still be looked into. The log may grow to 1 MB in total.

### Fixed
- Ads whose title ends with a space were muted only after about 1.5 seconds, so their first words were audible.
- Ads shown with a bare dash as title are muted as soon as the title appears, without waiting for the Windows media display.
- A Spotify that was closed while the PC slept is noticed after waking up, so "Pause Spotify" no longer fails on every video.
- When Spotify shows the next song for a split second in the middle of an ad break, the ad stays muted instead of a short burst of sound.
- When the settings file cannot be read or written, one change shows one error message instead of two.
- "Copy details" in the crash dialog keeps the last 50 log lines even after a troubleshooting trace, instead of losing them to trace lines.
- The whole round slider knob can be grabbed and dragged, not only its middle.

## [1.2.0] - 2026-10-29

### Added
- The settings window shows at the top how many ads were muted so far, whether an ad is being muted right now, and an on/off switch.
- The tray menu can hide the icon. Opening AdMutify again brings it back and opens the settings.
- Own music during ads, as the third choice next to muting and lowering: play one file, or a folder shuffled or in name order, at a volume you choose. The music fades in when the ad starts, fades out when it ends and continues at the same spot during the next ad.
- Music during ads plays OGG, OGA, OPUS, WEBM, MKA and MP2 files besides MP3, M4A, AAC, WMA, WAV and FLAC. OGG, OGA, OPUS and MKA need the Web Media Extensions that come with Windows (missing on N editions).
- Reset settings asks with a second click on the button instead of a dialog.
- Setup turns on "Start with Windows" by default on a first install for your own account; the box can be cleared on the same page.
- The volume slider for "Lower volume" only shows while that choice is selected; the video and language boxes are a bit taller.
- The settings window starts with the choices for ads and videos, then the language; the start options moved to the bottom.
- Troubleshooting: the hidden line `trace=1` in `settings.ini` turns on the trace like the `--trace` switch.

### Fixed
- An ad that was playing when Spotify was closed no longer keeps AdMutify in the muted state.
- Turning AdMutify off and on during an ad no longer counts that ad again.
- Ads whose title looks like a song are muted too.
- Spots titled just "Spotify" or a brand name are muted at once instead of after a few seconds.
- Choosing own music no longer opens a folder picker by itself; the source row below picks the file or folder.
- Podcast episodes are no longer muted as ads: their titles often lack " - ", so the Windows media info (the show as album) now tells them apart from spots.
- Spotify no longer stays quiet after an ad when it opens a new audio session during the ad, and a failed restore is retried within seconds instead of at the next ad.
- A failed mute during an ad is retried instead of showing "muted" while the ad plays.
- Old recovery records no longer undo a volume you set yourself later; locked recovery files are handled.
- Signing out while Spotify is being paused no longer leaves it paused.
- After a crash the report and dump are written even when restoring the volume hangs.
- The tray icon shows reliably when AdMutify is started twice in a row; unplugging an audio device updates at once.
- Setup: cancelling keeps AdMutify running, a silent update starts it again, an all-users setup notices AdMutify running for another user, and uninstalling only removes the autostart entry of the installed copy.
- Settings: concurrent saves are not lost, the window follows a changed Windows text size, and a settings file from a newer version is reported.
- A silent update closes a running AdMutify and starts it again instead of stopping; a silent portable setup needs its own folder and never turns an installed copy portable.
- The settings window fits the screen at high scaling or large text, and arrow keys, typed letters or the mouse wheel on a choice box no longer switch the choice by themselves.
- A large or sleeping network music folder no longer delays the restore after an ad.
- Turning off "Start with Windows" also clears Task Manager's entry; a recovery file from a newer version is kept.
- Translation details in German, Turkish, Japanese and Polish.

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
